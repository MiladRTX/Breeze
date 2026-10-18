/* breeze-guard.elf: puts the stock fan target back when breeze.elf ends
 * without its own exit (Payload Manager's SIGKILL, a crash) or its own
 * restore failed, which would otherwise leave the fan on Breeze's last
 * target until a game closes or starts.
 *
 * breeze.elf holds an flock on breeze.lock for as long as it runs and keeps
 * the targets it may have left in the fan and its stock value in it
 * ("<pid> <c256> <stock> <prev>", guardctl.h). The guard blocks on that
 * lock; the kernel drops it however breeze.elf ends. If the fan still holds
 * c256 or prev, nothing has changed it since and the guard writes that
 * stock value; if it holds anything else (the firmware reloaded, or
 * Breeze's crash handler got there first), it's left alone. Then the guard
 * exits: each breeze.elf starts its own (fc_guard_ensure). Started through
 * the ELF loader like breeze-tile.elf, links only kernel_sys and fan.c. */
#include "fan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#define LOCK "/data/breeze/breeze.lock"
#define GUARD_LOCK "/data/breeze/guard.lock"
#define LOG "/data/breeze/breeze.log"

int sceKernelSetProcessName(const char *name) __attribute__((weak));

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
        fprintf(f, "[%02d:%02d:%02d] guard: %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, msg);
        fclose(f);
    }
}

static void note(int fd, const char *line) {
    int n = (int)strlen(line);
    if (pwrite(fd, line, (size_t)n, 0) == n)
        (void)ftruncate(fd, n);
}

int main(void) {
    if (sceKernelSetProcessName)
        sceKernelSetProcessName("breeze-guard.elf");

    int gfd = open(GUARD_LOCK, O_RDWR | O_CREAT, 0666);
    if (gfd < 0 || flock(gfd, LOCK_EX | LOCK_NB) != 0)
        return 0; /* another guard is waiting already */
    int fd = open(LOCK, O_RDWR);
    if (fd < 0)
        return 0; /* Breeze never ran */

    while (flock(fd, LOCK_EX) != 0)
        if (errno != EINTR)
            return 1;

    /* breeze.elf is gone. */
    char buf[64] = {0};
    int c256 = -1, prev = -1; /* -1: no target of Breeze's in the fan */
    int stock = -1, fields = 0;
    if (pread(fd, buf, sizeof buf - 1, 0) > 0)
        fields = sscanf(buf, "%*d %d %d %d", &c256, &stock, &prev);
    /* Our pid in the lock right away: a Breeze starting now sees the guard
     * (not a dead pid) and waits for it, or stops it if it stalls. The
     * targets stay in the line until the fan is dealt with, so a Breeze
     * that had to stop this guard puts the fan back itself. */
    char line[64];
    snprintf(line, sizeof line, "%d -1\n", (int)getpid());
    if (c256 >= 0 || prev >= 0) {
        char rec[64];
        snprintf(rec, sizeof rec, "%d %d %d %d\n", (int)getpid(), c256, stock, prev);
        note(fd, rec);
    } else {
        note(fd, line);
    }

    if (c256 >= 0 || prev >= 0) {
        /* The stock value breeze.elf was using (it can be one it has not
         * saved yet); stock.conf or the default when the line has none or
         * one fan.c would not take. */
        if (fields < 2 || !fan_stock_ok(stock)) {
            fan_stock_load();
            stock = fan_stock_c256();
        }
        int now = fan_target_c256(), r = -1;
        if (now < 0) {
            logf_("could not read the fan, left alone");
        } else if (now != c256 && now != prev) {
            logf_("fan already changed, left alone");
            r = 0;
        } else if (now == stock) {
            logf_("breeze.elf ended on the stock target"); /* nothing to undo */
            r = 0;
        } else {
            r = fan_restore_if_ours(now, -1, stock);
            logf_("%s", r > 0    ? "restored stock after breeze.elf ended"
                        : r == 0 ? "fan already changed, left alone"
                                 : "could not set the fan, left alone");
        }
        if (r >= 0)
            note(fd, line);
    }

    /* Guard lock first: once Breeze can take breeze.lock, it must find no
     * guard and start a new one. */
    close(gfd);
    close(fd);
    return 0;
}
