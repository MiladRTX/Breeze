#include "app.h"
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *s_log;
static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;

void app_state_init(app_state *st) {
    memset(st, 0, sizeof *st);
    pthread_mutex_init(&st->lock, NULL);
    st->current_setpoint = -1;
}

void app_lock(app_state *st) {
    pthread_mutex_lock(&st->lock);
}

void app_unlock(app_state *st) {
    pthread_mutex_unlock(&st->lock);
}

void fc_log_open(void) {
    s_log = fopen(FC_LOG_PATH, "a");
}

void fc_log_close(void) {
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
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
    if (s_log) {
        fprintf(s_log, "[%02d:%02d:%02d] %s\n", tmv.tm_hour, tmv.tm_min,
                tmv.tm_sec, msg);
        fflush(s_log);
    }
    pthread_mutex_unlock(&s_log_lock);
}
