/* Breeze: standalone PS5 fan curve payload.
 *
 * Load it like any ps5-payload-sdk ELF (elfldr on port 9021, or Payload
 * Manager, which forwards to elfldr). It keeps running in the background,
 * steers the fan to the curve, and serves the web panel. */
#include "app.h"
#include "config.h"
#include "control.h"
#include "httpd.h"
#include "launcher.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define FC_LOCK_PATH FC_DATA_DIR "/breeze.lock"
/* The OnionHEN build of the same controller. Both writing the fan setpoint
 * would fight every 2 s, so Breeze refuses to start next to it. */
#define FC_ONION_PLUGIN "/data/OnionHEN/plugins/FNCR00001.elf"

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
/* elfldr names every payload process "payload.elf"; weak so a firmware
 * without it still loads Breeze. */
int sceKernelSetProcessName(const char *name) __attribute__((weak));

static app_state g_state;
static int srv_port;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void notify(const char *fmt, ...) {
    notify_request_t req;
    va_list ap;

    memset(&req, 0, sizeof req);
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof req.message, fmt, ap);
    va_end(ap);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);

    for (char *p = req.message; *p; p++) /* one log line per entry */
        if (*p == '\n')
            *p = ' ';
    fc_log("notify: %s", req.message);
}

/* First up, non-loopback IPv4 address; link-local only as a fallback. */
static int local_ipv4(char *out, size_t n) {
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) != 0)
        return -1;
    int found = 0;
    for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET ||
            !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
            continue;
        struct in_addr a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr;
        int link_local = (ntohl(a.s_addr) >> 16) == 0xA9FE;
        if (found && link_local)
            continue;
        inet_ntop(AF_INET, &a, out, (socklen_t)n);
        found = link_local ? 1 : 2;
        if (found == 2)
            break;
    }
    freeifaddrs(ifs);
    return found ? 0 : -1;
}

/* Start, stop or move the web panel to match the config (the panel itself
 * can change web_enabled / web_port). */
static void ensure_server_sync(void) {
    curve_config cfg;

    app_lock(&g_state);
    cfg = g_state.cfg;
    app_unlock(&g_state);

    int want = cfg.web_enabled ? cfg.web_port : 0;
    if (want == srv_port)
        return;
    if (srv_port)
        httpd_stop();
    srv_port = 0;
    if (want && httpd_start(&g_state) == 0)
        srv_port = want;
}

/* Tries the lock every 100 ms for up to `ms`. */
static int wait_lock(int fd, int ms) {
    for (int t = 0; t < ms; t += 100) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0)
            return 0;
        usleep(100000);
    }
    return -1;
}

/* Stops the copy holding the lock and takes the lock over. SIGTERM first so
 * it restores the stock fan target and closes the panel; SIGKILL if it
 * hangs. Returns the replaced pid, 0 if the holder went away on its own,
 * or -1. */
static int take_over(int lock_fd) {
    int pid = 0;
    /* A copy that has only just taken the lock may not have written its pid
     * yet: never signal a stale one, wait for the real one instead. */
    for (int i = 0; i < 20; i++) {
        char buf[16] = {0};
        if (pread(lock_fd, buf, sizeof buf - 1, 0) > 0)
            pid = atoi(buf);
        if (pid > 1)
            break;
        if (flock(lock_fd, LOCK_EX | LOCK_NB) == 0)
            return 0;
        usleep(100000);
    }
    if (pid <= 1 || pid == getpid())
        return -1;

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH)
        return -1;
    if (wait_lock(lock_fd, 6000) == 0)
        return pid;
    kill(pid, SIGKILL);
    return wait_lock(lock_fd, 3000) == 0 ? pid : -1;
}

static void notify_started(int replaced) {
    curve_config cfg;
    app_lock(&g_state);
    cfg = g_state.cfg;
    app_unlock(&g_state);

    char ip[INET_ADDRSTRLEN];
    const char *mode = cfg.enabled ? "custom curve on" : "stock fan";
    const char *verb = replaced ? "restarted" : "running";
    /* With the curve off (always the case on a first start) the panel is
     * where you pick one. */
    const char *lead = cfg.enabled ? "Panel:" : "Pick a curve at";
    if (!cfg.web_enabled)
        notify("%s %s %s (%s)\nWeb panel is off", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode);
    else if (!srv_port)
        notify("%s %s %s (%s)\nWeb panel failed to start on port %d",
               FC_PLUGIN_NAME, FC_PLUGIN_VERSION, verb, mode, cfg.web_port);
    else if (local_ipv4(ip, sizeof ip) == 0)
        notify("%s %s %s (%s)\n%s http://%s:%d", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode, lead, ip, srv_port);
    else
        notify("%s %s %s (%s)\n%s port %d", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode, lead, srv_port);
}

static void *launcher_thread(void *arg) {
    fc_launcher_ensure((int)(intptr_t)arg);
    return NULL;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    /* A browser dropping the panel connection mid-response must not kill
     * the payload (FreeBSD's default SIGPIPE action terminates). */
    signal(SIGPIPE, SIG_IGN);
    if (sceKernelSetProcessName)
        sceKernelSetProcessName("breeze.elf");

    mkdir(FC_DATA_DIR, 0777);
    fc_log_open();

    if (access(FC_ONION_PLUGIN, F_OK) == 0) {
        notify("%s not started: the OnionHEN Fan Curve plugin is installed.\n"
               "Remove %s first.", FC_PLUGIN_NAME, FC_ONION_PLUGIN);
        fc_log_close();
        return 1;
    }

    /* One instance only, and the newest launch wins: if a copy is already
     * running, ask it to stop (it hands the fan back and frees the port),
     * then take its place. The lock dies with its process, so it is never
     * stale, and the pid inside is valid for as long as the lock is held. */
    int lock_fd = open(FC_LOCK_PATH, O_RDWR | O_CREAT, 0666);
    if (lock_fd < 0) {
        notify("%s could not open %s", FC_PLUGIN_NAME, FC_LOCK_PATH);
        fc_log_close();
        return 1;
    }
    int replaced = 0;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        int replaced_pid = take_over(lock_fd);
        if (replaced_pid < 0) {
            notify("%s is already running and would not stop", FC_PLUGIN_NAME);
            fc_log_close();
            return 1;
        }
        if (replaced_pid > 0) {
            fc_log("replaced running copy (pid %d)", replaced_pid);
            replaced = 1;
        }
    }
    /* Overwrite, then trim: truncating first left a moment where the file
     * was empty and a new launch could read no pid. */
    char pid_txt[16];
    int pid_len = snprintf(pid_txt, sizeof pid_txt, "%d\n", (int)getpid());
    if (pwrite(lock_fd, pid_txt, (size_t)pid_len, 0) == pid_len)
        (void)ftruncate(lock_fd, pid_len);

    app_state_init(&g_state);
    curve_config cfg;
    fc_config_load(&cfg);
    preset_store ps;
    fc_presets_load(&ps);
    app_lock(&g_state);
    g_state.cfg = cfg;
    g_state.presets = ps;
    app_unlock(&g_state);
    fc_log("%s %s starting (enabled=%d web=%d port=%d)", FC_PLUGIN_NAME,
           FC_PLUGIN_VERSION, cfg.enabled, cfg.web_enabled, cfg.web_port);

    pthread_t ctl_thread;
    if (pthread_create(&ctl_thread, NULL, fc_control_loop, &g_state) != 0) {
        notify("%s failed to start its fan thread", FC_PLUGIN_NAME);
        fc_log_close();
        return 1;
    }

    ensure_server_sync();
    notify_started(replaced);
    /* Home screen tile; a no-op once it's installed and current. */
    if (srv_port) {
        pthread_t lt;
        if (pthread_create(&lt, NULL, launcher_thread, (void *)(intptr_t)srv_port) == 0)
            pthread_detach(lt);
    }

    for (int tick = 0; g_running; tick++) {
        usleep(100000);
        if (tick % 20 == 0)
            ensure_server_sync();
    }

    /* Hand the fan back before anything slow. Payload Manager's kill sends
     * SIGTERM and SIGKILL 1.5 s later, which used to land mid-teardown and
     * leave the fan on Breeze's last target. Under the lock, so the control
     * thread (which re-checks `stopping` there) can't write after this. */
    app_lock(&g_state);
    g_state.stopping = 1;
    g_state.current_setpoint = -1;
    int was_on = g_state.cfg.enabled;
    if (was_on)
        fan_write_setpoint(FC_STOCK_TARGET_C);
    app_unlock(&g_state);
    if (was_on)
        fc_log("ctl: stock target restored on exit");

    if (srv_port) {
        httpd_stop();
        srv_port = 0;
    }
    pthread_join(ctl_thread, NULL);
    fc_log("stopped");
    fc_log_close();
    close(lock_fd);
    return 0;
}
