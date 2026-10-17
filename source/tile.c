/* breeze-tile.elf: one-shot installer for the "Breeze" home screen tile.
 *
 * Kept out of breeze.elf on purpose. The firmware's app installer can block
 * forever (and a process stuck in it can't be killed), so it runs in its own
 * throwaway process, the way ftpsrv installs its tile. It writes a web
 * launcher (category 65536, deeplink to the panel) to /user/app/BREZ00001,
 * registers it, logs to /data/breeze/breeze.log and exits. */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <ps5/kernel.h>

#include "tile_param.h"

#define TITLE_ID TILE_TITLE_ID
#define APP_DIR TILE_APP_DIR
#define SYS_DIR TILE_SYS_DIR
#define CONF "/data/breeze/breeze.conf"
#define LOG "/data/breeze/breeze.log"

extern const char kIcon0Png[];
extern const size_t kIcon0PngLen;

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void logf_(const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    FILE *f = fopen(LOG, "a");
    if (f) {
        fprintf(f, "[%02d:%02d:%02d] tile: %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, msg);
        fclose(f);
    }
}

static void notify(const char *msg) {
    notify_request_t req;
    memset(&req, 0, sizeof req);
    snprintf(req.message, sizeof req.message, "%s", msg);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

/* Panel port from Breeze's config, 2878 if unset. */
static int panel_port(void) {
    int port = 2878;
    char line[160];
    FILE *f = fopen(CONF, "r");
    if (!f)
        return port;
    /* Same parsing as the config loader, or the two can disagree on the
     * port and the tile gets reinstalled on every start. */
    while (fgets(line, sizeof line, f)) {
        char *l = line;
        while (*l == ' ' || *l == '\t')
            l++;
        if (!strncmp(l, "web_port=", 9)) {
            int p = atoi(l + 9);
            port = (p >= 1024 && p <= 65535) ? p : 2878;
        }
    }
    fclose(f);
    return port;
}

static int write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, len, f) == len;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

/* The single-title installer has no exported name; look it up by NID and
 * fall back to rescanning /user/app on firmware without it. */
static int register_title(void) {
    int (*install_dir)(const char *, const char *, void *) = NULL;
    uint32_t h;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) == 0)
        install_dir = (void *)kernel_dynlib_resolve(-1, h, "Wudg3Xe3heE");
    if (install_dir)
        return install_dir(TITLE_ID, "/user/app/", NULL);
    return sceAppInstUtilAppInstallAll(NULL);
}

int main(void) {
    char param[512];
    int port = panel_port();
    int plen = tile_param(param, sizeof param, port);

    logf_("starting (port %d)", port);
    int err = sceAppInstUtilInitialize();
    if (err) {
        logf_("installer init failed (0x%08X)", err);
        notify("Breeze: couldn't add the home screen tile");
        return 1;
    }
    logf_("installer ready");

    mkdir("/user/app", 0755);
    mkdir(APP_DIR, 0755);
    mkdir(SYS_DIR, 0755);
    if (write_file(SYS_DIR "/param.json", param, (size_t)plen) ||
        write_file(SYS_DIR "/icon0.png", kIcon0Png, kIcon0PngLen)) {
        logf_("cannot write tile files (errno %d)", errno);
        notify("Breeze: couldn't add the home screen tile");
        sceAppInstUtilTerminate();
        return 1;
    }
    logf_("files written, registering");

    err = register_title();
    sceAppInstUtilTerminate();
    if (err) {
        logf_("register failed (0x%08X)", err);
        notify("Breeze: couldn't add the home screen tile");
        return 1;
    }
    logf_("installed");
    notify("Breeze added to your home screen");
    return 0;
}
