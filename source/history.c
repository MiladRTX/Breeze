#include "history.h"

#include <pthread.h>
#include <stdio.h>

typedef struct sample {
    signed char cpu, duty, want;
} sample;

static sample s_ring[FC_HIST_CAP];
static int s_head; /* next write slot */
static int s_count;
static int s_tick;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static signed char clamp_val(int v) {
    if (v < 0)
        return -1;
    return (signed char)(v > 127 ? 127 : v);
}

void fc_hist_record(int cpu_c, int duty_pct, int want_pct) {
    /* Called once per second by the fan loop; keep every Nth. */
    if (++s_tick < FC_HIST_PERIOD_S)
        return;
    s_tick = 0;

    pthread_mutex_lock(&s_lock);
    s_ring[s_head] = (sample){clamp_val(cpu_c), clamp_val(duty_pct),
                              clamp_val(want_pct)};
    s_head = (s_head + 1) % FC_HIST_CAP;
    if (s_count < FC_HIST_CAP)
        s_count++;
    pthread_mutex_unlock(&s_lock);
}

static int avg(int sum, int n) {
    return n ? (sum + n / 2) / n : -1;
}

size_t fc_hist_json(char *buf, size_t cap, int seconds, int max_points) {
    size_t off = 0;
    if (cap < 32)
        return 0;
    if (max_points < 1)
        max_points = 1;

    pthread_mutex_lock(&s_lock);
    int avail = seconds / FC_HIST_PERIOD_S;
    if (avail > s_count)
        avail = s_count;
    if (avail < 0)
        avail = 0;
    int per = (avail + max_points - 1) / max_points;
    if (per < 1)
        per = 1;
    int buckets = (avail + per - 1) / per;
    /* Buckets are aligned to the newest sample; a partial one goes first. */
    int first = avail - (buckets - 1) * per;
    int idx = (s_head - avail + FC_HIST_CAP) % FC_HIST_CAP;

    off += (size_t)snprintf(buf + off, cap - off, "{\"step\":%d,\"d\":[",
                            per * FC_HIST_PERIOD_S);
    for (int b = 0; b < buckets && off < cap; b++) {
        int n = b == 0 ? first : per;
        int sc = 0, nc = 0, sd = 0, nd = 0, sw = 0, nw = 0;
        for (int k = 0; k < n; k++) {
            sample s = s_ring[idx];
            idx = (idx + 1) % FC_HIST_CAP;
            if (s.cpu >= 0) { sc += s.cpu; nc++; }
            if (s.duty >= 0) { sd += s.duty; nd++; }
            if (s.want >= 0) { sw += s.want; nw++; }
        }
        off += (size_t)snprintf(buf + off, cap - off, "%s[%d,%d,%d]",
                                b ? "," : "", avg(sc, nc), avg(sd, nd),
                                avg(sw, nw));
    }
    pthread_mutex_unlock(&s_lock);

    if (off < cap)
        off += (size_t)snprintf(buf + off, cap - off, "]}\n");
    if (off >= cap) /* truncated: never hand out half a document */
        off = (size_t)snprintf(buf, cap, "{\"step\":0,\"d\":[]}\n");
    return off;
}
