/* Breeze: standalone PS5 fan curve payload.
 *
 * Load it like any ps5-payload-sdk ELF (elfldr on port 9021, or Payload
 * Manager, which forwards to elfldr). It keeps running in the background,
 * steers the fan to the curve, and serves the web panel. */
#include "app.h"
#include "config.h"
#include "control.h"
#include "guardctl.h"
#include "history.h"
#include "httpd.h"
#include "launcher.h"
#include "update.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/user.h>
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

/* Rest mode closes every listening socket: the panel's port then refuses
 * connections although Breeze, and the fan control, kept running. The
 * address check in the main loop catches it (the address goes on the way
 * into rest mode and comes back on waking); SIGCONT, on which Payload
 * Manager restarts its server, is kept as a second trigger. */
static volatile sig_atomic_t g_resumed;

static void on_resume(int sig) {
    (void)sig;
    g_resumed = 1;
}

void fc_notify(const char *fmt, ...) {
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

/* Closes the panel so the main loop opens it again on a fresh socket a
 * second later, as Payload Manager does after rest mode: the old socket is
 * dead, and the network needs a moment. */
static void close_server_for_reopen(const char *why) {
    if (!srv_port)
        return;
    fc_log("web: %s, reopening the panel", why);
    httpd_stop();
    srv_port = 0;
}

/* The console's address as last seen, "" for none. */
static char g_addr[INET_ADDRSTRLEN];

/* 1 when the address is not the one last seen: Wi-Fi back after rest mode,
 * a new DHCP lease, a cable plugged in or pulled. */
static int address_changed(void) {
    char now[INET_ADDRSTRLEN] = "";
    if (local_ipv4(now, sizeof now) != 0)
        now[0] = 0;
    if (!strcmp(now, g_addr))
        return 0;
    fc_log("net: address %s -> %s", g_addr[0] ? g_addr : "none", now[0] ? now : "none");
    snprintf(g_addr, sizeof g_addr, "%s", now);
    return 1;
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

/* What `pid` is: 1 breeze.elf, 2 breeze-guard.elf, 0 anything else or gone,
 * -1 unknown (the lookup failed). Its name goes to `comm`, "" if none. */
static int proc_kind(int pid, char *comm, size_t n) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc kp;
    size_t len = sizeof kp;
    memset(&kp, 0, sizeof kp);
    comm[0] = 0;
    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 && errno != ENOMEM)
        return errno == ESRCH ? 0 : -1;
    if (len < offsetof(struct kinfo_proc, ki_comm) + sizeof kp.ki_comm)
        return len == 0 ? 0 : -1;
    kp.ki_comm[sizeof kp.ki_comm - 1] = 0;
    snprintf(comm, n, "%s", kp.ki_comm);
    if (!strcmp(kp.ki_comm, "breeze.elf"))
        return 1;
    return !strcmp(kp.ki_comm, "breeze-guard.elf") ? 2 : 0;
}

/* The pid in the lock file once it is a breeze.elf or breeze-guard.elf
 * (`*kind` from proc_kind), tried for 2 s. A copy that has only just taken
 * the lock may not have written its pid yet, and the pid of one that died
 * can belong to anything by now: those are never signalled. 0 if the lock
 * came free (it is ours now), -1 if no such holder showed up. */
static int find_holder(int lock_fd, int *kind) {
    char comm[32];
    int pid = 0;
    for (int i = 0; i < 20; i++) {
        char buf[16] = {0};
        pid = 0;
        comm[0] = 0;
        if (pread(lock_fd, buf, sizeof buf - 1, 0) > 0)
            pid = atoi(buf);
        if (pid > 1 && pid != getpid() &&
            (*kind = proc_kind(pid, comm, sizeof comm)) != 0)
            return pid;
        if (flock(lock_fd, LOCK_EX | LOCK_NB) == 0)
            return 0;
        usleep(100000);
    }
    /* The name tells a stale pid from a process rename that didn't apply. */
    if (comm[0])
        fc_log("take-over: lock pid %d is '%s', not signalled", pid, comm);
    else
        fc_log("take-over: lock still held, pid %d in it is not running", pid);
    return -1;
}

/* Stops the copy holding the lock and takes the lock over. SIGTERM first so
 * it restores the stock fan target and closes the panel; SIGKILL if it
 * hangs. A guard holding the lock is restoring the fan after a copy that
 * died: it gets a moment to finish, then SIGTERM (which ends it). The
 * holder is looked up again before each signal, since the guard takes the
 * lock when a copy dies. Returns the replaced pid, 0 if the holder went away
 * on its own, or -1. */
static int take_over(int lock_fd) {
    int replaced = 0, termed = 0;
    for (int round = 0; round < 3; round++) {
        int kind = 0, pid = find_holder(lock_fd, &kind);
        if (pid <= 0)
            return pid < 0 ? -1 : replaced;
        if (pid == termed) { /* still there after SIGTERM */
            kill(pid, SIGKILL);
            return wait_lock(lock_fd, 3000) == 0 ? replaced : -1;
        }
        if (kind == 2 && wait_lock(lock_fd, 3000) == 0)
            return replaced;
        if (kill(pid, SIGTERM) != 0 && errno != ESRCH)
            return -1;
        if (!replaced)
            replaced = pid;
        termed = pid;
        if (wait_lock(lock_fd, kind == 2 ? 3000 : 6000) == 0)
            return replaced;
    }
    return -1;
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
    const char *lead = cfg.enabled ? "Panel:" : "Turn the curve on at";
    if (!cfg.web_enabled)
        fc_notify("%s %s %s (%s)\nWeb panel is off", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode);
    else if (!srv_port)
        fc_notify("%s %s %s (%s)\nWeb panel failed to start on port %d",
               FC_PLUGIN_NAME, FC_PLUGIN_VERSION, verb, mode, cfg.web_port);
    else if (local_ipv4(ip, sizeof ip) == 0)
        fc_notify("%s %s %s (%s)\n%s http://%s:%d", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode, lead, ip, srv_port);
    else
        fc_notify("%s %s %s (%s)\n%s port %d", FC_PLUGIN_NAME,
               FC_PLUGIN_VERSION, verb, mode, lead, srv_port);
}

static volatile int s_launching; /* a launcher thread is running */

static void *launcher_thread(void *arg) {
    int port = (int)(intptr_t)arg;
    if (port)
        fc_launcher_ensure(port);
    fc_guard_ensure();
    s_launching = 0;
    return NULL;
}

/* Home screen tile (port 0: skip it), then the guard. On its own thread:
 * the loader can take seconds, and the main thread has to stay free for
 * the stock restore on SIGTERM. One at a time. */
static void start_launcher(int port) {
    if (s_launching)
        return;
    s_launching = 1;
    pthread_t lt;
    if (pthread_create(&lt, NULL, launcher_thread, (void *)(intptr_t)port) == 0)
        pthread_detach(lt);
    else
        s_launching = 0;
}

/* What the previous holder left in breeze.lock ("<pid> <c256> <stock>
 * <prev>", guardctl.h; older lines have fewer fields). A target there means
 * it ended with nothing to put the fan back: no guard waiting, a guard
 * stopped before it was done, or a restore that failed. */
typedef struct lock_record {
    int c256, stock, prev;
} lock_record;

static lock_record read_record(int lock_fd) {
    lock_record r = {-1, -1, -1};
    char buf[64] = {0};
    if (pread(lock_fd, buf, sizeof buf - 1, 0) > 0)
        sscanf(buf, "%*d %d %d %d", &r.c256, &r.stock, &r.prev);
    return r;
}

/* After fan_stock_load: its stock value if the line has a usable one (the
 * guard does the same), else ours. Until this is done the record stays in
 * breeze.lock; if the restore fails it stays as this copy's own failed
 * restore, with that stock, and is retried like one (the control loop with
 * the curve off, the exit, the crash handler, the guard, the next start). */
static void restore_left(lock_record left) {
    if (left.c256 < 0 && left.prev < 0)
        return;
    int stock = fan_stock_ok(left.stock) ? left.stock : fan_stock_c256();
    app_lock(&g_state);
    int r = fan_restore_if_ours(left.c256, left.prev, stock);
    if (r >= 0) {
        fc_lock_note_target(-1);
    } else {
        fan_stock_use(stock);
        g_state.last_c256 = left.c256; /* a failed SET of the curve keeps it too */
        fc_lock_note_left(left.c256, left.prev); /* the stock in use, too */
    }
    app_unlock(&g_state);
    if (r > 0)
        fc_log("a stopped copy left its fan target behind: stock target %d.%02dC put back",
               stock / 256, (stock % 256) * 100 / 256);
    else
        fc_log("a stopped copy left its fan target behind, %s",
               r == 0 ? "but the fan had already changed"
                      : "and putting stock back failed: kept for a retry");
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    /* A browser dropping the panel connection mid-response must not kill
     * the payload (FreeBSD's default SIGPIPE action terminates). */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCONT, on_resume);
    fc_crash_handler_install();
    if (sceKernelSetProcessName)
        sceKernelSetProcessName("breeze.elf");

    mkdir(FC_DATA_DIR, 0777);
    fc_log_open();

    if (access(FC_ONION_PLUGIN, F_OK) == 0) {
        fc_notify("%s not started: the OnionHEN Fan Curve plugin is installed.\n"
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
        fc_notify("%s could not open %s", FC_PLUGIN_NAME, FC_LOCK_PATH);
        fc_log_close();
        return 1;
    }
    int replaced_pid = 0;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        replaced_pid = take_over(lock_fd);
        if (replaced_pid < 0) {
            fc_notify("%s is already running and would not stop", FC_PLUGIN_NAME);
            fc_log_close();
            return 1;
        }
    }
    lock_record left = read_record(lock_fd);
    /* Our pid in the line at once (take_over reads it). Any target the
     * previous holder left stays in it until restore_left has dealt with
     * it, so a kill before then loses nothing (guardctl.h). */
    g_lock_fd = lock_fd;
    fc_lock_note_left(left.c256, left.prev);

    /* Holding the lock: only now may this copy rotate the log, and only
     * now are its lines sure to land in the live file. */
    fc_log_own();
    int replaced = replaced_pid > 0;
    if (replaced)
        fc_log("replaced running copy (pid %d)", replaced_pid);

    app_state_init(&g_state);
    curve_config cfg;
    fc_config_load(&cfg);
    fan_stock_load();
    preset_store ps;
    fc_presets_load(&ps);
    fc_presets_fix_active(&ps, &cfg);
    app_lock(&g_state);
    g_state.cfg = cfg;
    g_state.presets = ps;
    app_unlock(&g_state);
    fc_log("%s %s starting (enabled=%d web=%d port=%d)", FC_PLUGIN_NAME,
           FC_PLUGIN_VERSION, cfg.enabled, cfg.web_enabled, cfg.web_port);
    restore_left(left);

    /* SIGTERM or SIGINT while taking over: the old copy is gone, so no
     * panel and no "running" notification. This copy wrote nothing; a
     * target restore_left could not put back stays in breeze.lock for the
     * copy that sent the SIGTERM, or the next one to start. */
    if (!g_running) {
        fc_log("stopped before starting");
        fc_log_close();
        g_lock_fd = -1;
        close(lock_fd);
        return 0;
    }

    /* After the lock (a replaced copy has saved on its way out), before the
     * control thread records. */
    fc_hist_load();
    fc_hist_start_saver();

    pthread_t ctl_thread;
    if (pthread_create(&ctl_thread, NULL, fc_control_loop, &g_state) != 0) {
        fc_notify("%s failed to start its fan thread", FC_PLUGIN_NAME);
        fc_log_close();
        return 1;
    }

    ensure_server_sync();
    notify_started(replaced);
    /* Tile with the panel on (a no-op once it's installed and current). */
    start_launcher(srv_port);
    fc_update_start(&g_state);

    if (local_ipv4(g_addr, sizeof g_addr) != 0)
        g_addr[0] = 0;
    int reopen_tick = -1; /* when a closed panel opens again */
    for (int tick = 0; g_running; tick++) {
        usleep(100000);
        if (!g_running) /* signalled: nothing else before the restore */
            break;
        if (g_resumed) {
            g_resumed = 0;
            close_server_for_reopen("the console woke from rest mode");
            reopen_tick = tick + 10;
        }
        if (tick % 50 == 25 && address_changed()) {
            close_server_for_reopen("the network changed");
            reopen_tick = tick + 10;
        }
        if (tick == reopen_tick || (tick % 20 == 0 && tick > reopen_tick))
            ensure_server_sync();
        /* 10 s: restart a guard that was killed. The probe is one LOCK_NB;
         * only the loader send goes to a thread. */
        if (tick % 100 == 99 && fc_guard_probe() == 0)
            start_launcher(0);
    }

    /* Hand the fan back before anything slow. Payload Manager's kill is a
     * bare SIGKILL that breeze-guard.elf handles; this order is for
     * take-over's SIGTERM (SIGKILL follows after 6 s) and SIGINT. Under the
     * lock, so the control thread (which re-checks `stopping` there) can't
     * write after this. */
    app_lock(&g_state);
    g_state.stopping = 1;
    int restored = fc_fan_restore_locked(&g_state);
    app_unlock(&g_state);
    if (restored > 0)
        fc_log("ctl: stock target restored on exit (%d.%02dC)", fan_stock_c256() / 256,
               (fan_stock_c256() % 256) * 100 / 256);
    else if (restored < 0)
        fc_log("ctl: could not restore the stock target on exit, left to breeze-guard.elf");

    /* Fan handed back; the file I/O comes after it, and before the slow
     * part of the teardown that a SIGKILL after the SIGTERM may cut short. */
    fc_hist_save();

    if (srv_port) {
        httpd_stop();
        srv_port = 0;
    }
    pthread_join(ctl_thread, NULL);
    fc_log("stopped");
    fc_log_close();
    g_lock_fd = -1;
    close(lock_fd);
    return 0;
}
