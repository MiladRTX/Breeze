#include "app.h"
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *s_log;
static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_log_yday = -1; /* day of the last line, for the date marker */
static int s_log_owner;     /* holds breeze.lock: the only copy that rotates */
static int s_log_lost;      /* reopening after a rotation failed: retried */
static int s_log_retry;

/* The log is capped: past FC_LOG_MAX bytes it moves to breeze.log.1
 * (replacing the previous one) and a fresh file starts, so at most about
 * twice that stays on /data. Fine control logs every small target step
 * while a game runs (~25 KB/h), so uncapped it grew by ~0.5 MB a day. */
#define FC_LOG_MAX (256 * 1024)
#define FC_LOG_OLD FC_LOG_PATH ".1"
#define FC_LOG_RETRY_LINES 64

/* Caller holds s_log_lock. Only the lock holder rotates: a new launch opens
 * the log before it takes over, and if the copy it replaces rotated while
 * logging its exit, a second rotation from the new launch's stale stream
 * replaced breeze.log.1 and lost the whole previous log. */
static void log_rotate_if_big(void) {
    if (!s_log || !s_log_owner)
        return;
    long size = ftell(s_log);
    if (size < FC_LOG_MAX)
        return;
    fclose(s_log);
    rename(FC_LOG_PATH, FC_LOG_OLD);
    s_log = fopen(FC_LOG_PATH, "a");
    s_log_lost = !s_log;
    s_log_yday = -1; /* the new file starts with a date line */
}

void app_state_init(app_state *st) {
    memset(st, 0, sizeof *st);
    pthread_mutex_init(&st->lock, NULL);
    st->current_setpoint = -1;
    st->console_min = -1;
    st->last_c256 = -1;
    for (int i = 0; i < FAN_CHANNELS; i++)
        st->ch[i].target_c256 = st->ch[i].temp_c256 = st->ch[i].duty_q10 = -1;
}

void app_lock(app_state *st) {
    pthread_mutex_lock(&st->lock);
}

void app_unlock(app_state *st) {
    pthread_mutex_unlock(&st->lock);
}

void fc_log_open(void) {
    pthread_mutex_lock(&s_log_lock);
    s_log = fopen(FC_LOG_PATH, "a");
    s_log_yday = -1;
    pthread_mutex_unlock(&s_log_lock);
}

void fc_log_own(void) {
    pthread_mutex_lock(&s_log_lock);
    s_log_owner = 1;
    /* The replaced copy may have rotated meanwhile, leaving this stream on
     * breeze.log.1: reopen by path. */
    if (s_log)
        fclose(s_log);
    s_log = fopen(FC_LOG_PATH, "a");
    s_log_lost = !s_log;
    s_log_yday = -1;
    /* append mode: the position is only defined after a write */
    if (s_log && fseek(s_log, 0, SEEK_END) == 0)
        log_rotate_if_big();
    pthread_mutex_unlock(&s_log_lock);
}

void fc_log_close(void) {
    /* under the lock: the detached launcher thread may still be logging */
    pthread_mutex_lock(&s_log_lock);
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    s_log_lost = 0; /* closed on purpose: stays closed */
    pthread_mutex_unlock(&s_log_lock);
}

void fc_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    pthread_mutex_lock(&s_log_lock);
    printf("[%02d:%02d:%02d] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msg);
    fflush(stdout);
    if (s_log_lost && ++s_log_retry >= FC_LOG_RETRY_LINES) {
        s_log_retry = 0; /* /data was full or the file was in the way */
        s_log = fopen(FC_LOG_PATH, "a");
        s_log_lost = !s_log;
        s_log_yday = -1;
    }
    if (s_log) {
        /* lines only carry the time: mark each new day (and each new file) */
        if (tmv.tm_yday != s_log_yday) {
            fprintf(s_log, "--- %04d-%02d-%02d ---\n", tmv.tm_year + 1900,
                    tmv.tm_mon + 1, tmv.tm_mday);
            s_log_yday = tmv.tm_yday;
        }
        fprintf(s_log, "[%02d:%02d:%02d] %s\n", tmv.tm_hour, tmv.tm_min,
                tmv.tm_sec, msg);
        fflush(s_log);
        log_rotate_if_big();
    }
    pthread_mutex_unlock(&s_log_lock);
}
