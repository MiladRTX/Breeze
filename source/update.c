/* Update check: with update_check on, Breeze asks GitHub for its latest
 * release a minute after it starts and once a day after that (an hour when
 * the check failed, say offline). A newer version goes to /api/state as
 * "latest" for the app and the panel, and gets one PS5 notification per
 * version (the last one told is kept in update.txt). Nothing is downloaded
 * or installed.
 *
 * HTTPS is the console's own (SceHttp2 over SceSsl, as the SDK's http2_get
 * sample), on this thread: a slow or dead network never holds up the fan. */
#include "update.h"
#include "config.h"
#include "version.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FC_UPDATE_URL "https://api.github.com/repos/MiladRTX/Breeze/releases/latest"
#define FC_UPDATE_TOLD FC_DATA_DIR "/update.txt"
#define FC_UPDATE_FIRST 60      /* s after start: the network may still be coming up */
#define FC_UPDATE_EVERY 86400   /* s between checks */
#define FC_UPDATE_RETRY 3600    /* s after a failed check */

int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(size_t);
int sceSslTerm(int);
int sceHttp2Init(int, int, size_t, int);
int sceHttp2Term(int);
int sceHttp2CreateTemplate(int, const char *, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void *, size_t);
int sceHttp2GetStatusCode(int, int *);
int sceHttp2ReadData(int, void *, size_t);

/* The latest release's version into tag. Returns 0, or the step that failed
 * (1-8) with its error code or HTTP status in *code, for the log. */
static int fetch_latest(char *tag, size_t n, int *code) {
    static char body[16384]; /* tag_name comes in the first few KB */
    int mem = -1, ssl = -1, http = -1, tmpl = -1, req = -1, step = 0, r = 0;
    size_t got = 0;

    *code = 0;
    if ((r = mem = sceNetPoolCreate("breeze-update", 32 * 1024, 0)) < 0) {
        step = 1;
        goto out;
    }
    if ((r = ssl = sceSslInit(256 * 1024)) < 0) {
        step = 2;
        goto out;
    }
    if ((r = http = sceHttp2Init(mem, ssl, 256 * 1024, 1)) < 0) {
        step = 3;
        goto out;
    }
    if ((r = tmpl = sceHttp2CreateTemplate(http, FC_PLUGIN_NAME "/" FC_PLUGIN_VERSION, 3, 1)) < 0) {
        step = 4;
        goto out;
    }
    if ((r = req = sceHttp2CreateRequestWithURL(tmpl, "GET", FC_UPDATE_URL, 0)) < 0) {
        step = 5;
        goto out;
    }
    if ((r = sceHttp2SendRequest(req, NULL, 0)) != 0) {
        step = 6;
        goto out;
    }
    if ((r = sceHttp2GetStatusCode(req, code)) != 0 || *code != 200) {
        step = 7;
        goto out;
    }
    step = 8;
    while (got + 1 < sizeof body) {
        int len = sceHttp2ReadData(req, body + got, sizeof body - 1 - got);
        if (len <= 0)
            break;
        got += (size_t)len;
        body[got] = 0;
        if (fc_release_tag(body, tag, n) == 0) {
            step = 0;
            break;
        }
    }
out:
    if (step && step < 7)
        *code = r;
    if (req >= 0)
        sceHttp2DeleteRequest(req);
    if (tmpl >= 0)
        sceHttp2DeleteTemplate(tmpl);
    if (http >= 0)
        sceHttp2Term(http);
    if (ssl >= 0)
        sceSslTerm(ssl);
    if (mem >= 0)
        sceNetPoolDestroy(mem);
    return step;
}

static void told_load(char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(FC_UPDATE_TOLD, "r");
    if (!f)
        return;
    if (fgets(out, (int)n, f))
        out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

static void told_save(const char *ver) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", FC_UPDATE_TOLD);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", ver);
    (void)fc_file_commit(f, tmp, FC_UPDATE_TOLD);
}

static void *update_thread(void *arg) {
    app_state *st = arg;
    char told[16], tag[16];
    told_load(told, sizeof told);

    /* Fails harmlessly when the process already has the network up. */
    int r = sceNetInit();
    if (r != 0)
        fc_log("update: sceNetInit 0x%x", (unsigned)r);

    time_t next = time(NULL) + FC_UPDATE_FIRST;
    int was_on = 1, failed = 0;
    for (;;) {
        sleep(10);
        app_lock(st);
        int on = st->cfg.update_check;
        app_unlock(st);
        time_t now = time(NULL);
        if (!on) {
            was_on = 0;
            continue;
        }
        if (!was_on) /* just switched on: check now */
            next = now;
        was_on = 1;
        if (next - now > FC_UPDATE_EVERY) /* the clock went back */
            next = now;
        if (now < next)
            continue;

        int code = 0, step = fetch_latest(tag, sizeof tag, &code);
        if (step) {
            /* Once per run of failures: offline, it would log every hour. */
            if (!failed++)
                fc_log("update: check failed at step %d (0x%x), retrying hourly", step,
                       (unsigned)code);
            next = now + FC_UPDATE_RETRY;
            continue;
        }
        failed = 0;
        next = now + FC_UPDATE_EVERY;

        int newer = fc_version_cmp(tag, FC_PLUGIN_VERSION) > 0;
        app_lock(st);
        snprintf(st->latest, sizeof st->latest, "%s", newer ? tag : "");
        on = st->cfg.update_check;
        app_unlock(st);
        fc_log("update: latest release %s, running %s", tag, FC_PLUGIN_VERSION);
        if (newer && on && strcmp(told, tag) != 0) {
            fc_notify("%s %s is out\nGet it in ProsperoStore or github.com/MiladRTX/Breeze",
                      FC_PLUGIN_NAME, tag);
            snprintf(told, sizeof told, "%s", tag);
            told_save(told);
        }
    }
    return NULL;
}

void fc_update_start(app_state *st) {
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* The TLS handshake runs on this thread's stack. */
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, update_thread, st) != 0)
        fc_log("update: could not start the check thread");
    pthread_attr_destroy(&attr);
}
