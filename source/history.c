#include "history.h"
#include "app.h"
#include "config.h"
#include "control.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct sample {
    signed char cpu, duty, want;
} sample;
_Static_assert(sizeof(sample) == 3, "sample must pack to 3 bytes");

/* history.bin: this header, then `count` samples oldest first. Native
 * little-endian layout, read back only by Breeze on the same console; a
 * layout change must bump HIST_FILE_VER (older files are then discarded). */
#define HIST_MAGIC "BZH1"
#define HIST_FILE_VER 1
typedef struct hist_file_hdr {
    char magic[4];       /* HIST_MAGIC */
    uint16_t version;    /* HIST_FILE_VER */
    uint16_t period_s;   /* FC_HIST_PERIOD_S when written */
    uint32_t count;      /* samples that follow, 1..FC_HIST_CAP */
    uint32_t sum;        /* FNV-1a of this header (sum = 0) and the samples */
    int64_t newest_rt;   /* CLOCK_REALTIME s of the newest sample */
    int64_t newest_mono; /* CLOCK_MONOTONIC s of the newest sample */
} hist_file_hdr;
_Static_assert(sizeof(hist_file_hdr) == 32, "history header layout");

static sample s_ring[FC_HIST_CAP];
static int s_head; /* next write slot */
static int s_count;
static int64_t s_last_slot = -1;       /* monotonic slot of the newest sample */
static int64_t s_last_rt, s_last_mono; /* clocks when it was taken */
static int64_t s_saved_slot = -1;      /* newest slot already on disk */
static int s_save_failed;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
/* Serializes whole saves (one temp file); held across the write, so never
 * taken under s_lock. Guards snap, s_saved_slot and s_save_failed. */
static pthread_mutex_t s_save_lock = PTHREAD_MUTEX_INITIALIZER;

static const sample k_gap = {FC_HIST_GAP, FC_HIST_GAP, FC_HIST_GAP};

static signed char clamp_val(int v) {
    if (v < 0)
        return -1;
    return (signed char)(v > 127 ? 127 : v);
}

static void push_locked(sample s) {
    s_ring[s_head] = s;
    s_head = (s_head + 1) % FC_HIST_CAP;
    if (s_count < FC_HIST_CAP)
        s_count++;
}

static void push_gap_locked(int64_t n) {
    if (n >= FC_HIST_CAP) { /* everything older is off the chart */
        s_head = 0;
        s_count = 0;
        return;
    }
    while (n-- > 0)
        push_locked(k_gap);
}

static uint32_t fnv1a(uint32_t h, const void *p, size_t n) {
    const unsigned char *b = p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

static uint32_t file_sum(hist_file_hdr h, const sample *s, size_t n) {
    h.sum = 0;
    return fnv1a(fnv1a(2166136261u, &h, sizeof h), s, n * sizeof *s);
}

static void now_s(int64_t *rt, int64_t *mono) {
    struct timespec r = {0}, m = {0};
    clock_gettime(CLOCK_REALTIME, &r);
    clock_gettime(CLOCK_MONOTONIC, &m);
    *rt = (int64_t)r.tv_sec;
    *mono = (int64_t)m.tv_sec;
}

void fc_hist_record(int cpu_c, int duty_pct, int want_pct) {
    int64_t rt, mono;
    now_s(&rt, &mono);
    int64_t slot = mono / FC_HIST_PERIOD_S;
    int64_t idle = 0, drift = 0;

    pthread_mutex_lock(&s_lock);
    if (s_last_slot >= 0 && slot <= s_last_slot) {
        pthread_mutex_unlock(&s_lock); /* one sample per period */
        return;
    }
    sample s = {clamp_val(cpu_c), clamp_val(duty_pct), clamp_val(want_pct)};
    if (s_last_slot >= 0) {
        /* Slots follow the monotonic clock only: the wall clock is stepped
         * by the PS5 settings and by other payloads. One missed slot is a
         * long tick, not an outage, so it repeats the sample. If rest mode
         * stops the monotonic clock, rest time is not drawn as a gap. */
        int64_t missed = slot - s_last_slot - 1;
        if (missed == 1)
            push_locked(s);
        else
            push_gap_locked(missed);
        idle = mono - s_last_mono;
        drift = (rt - s_last_rt) - idle;
    }
    push_locked(s);
    s_last_slot = slot;
    s_last_rt = rt;
    s_last_mono = mono;
    pthread_mutex_unlock(&s_lock);

    /* Diagnostics only: after a rest cycle they tell whether the process
     * was frozen and which clock kept counting. */
    if (idle >= 10)
        fc_log("hist: no sample for %llds", (long long)idle);
    if (drift >= 10 || drift <= -10)
        fc_log("hist: wall clock moved %+llds vs monotonic", (long long)drift);
}

static int save_locked(void) {
    static sample snap[FC_HIST_CAP];
    hist_file_hdr h;
    memset(&h, 0, sizeof h);

    pthread_mutex_lock(&s_lock);
    int n = s_count;
    int64_t slot = s_last_slot;
    int idx = (s_head - n + FC_HIST_CAP) % FC_HIST_CAP;
    for (int i = 0; i < n; i++) {
        snap[i] = s_ring[idx];
        idx = (idx + 1) % FC_HIST_CAP;
    }
    h.newest_rt = s_last_rt;
    h.newest_mono = s_last_mono;
    pthread_mutex_unlock(&s_lock);

    if (n == 0 || slot == s_saved_slot)
        return 0; /* nothing new: spare the flash */

    memcpy(h.magic, HIST_MAGIC, 4);
    h.version = HIST_FILE_VER;
    h.period_s = FC_HIST_PERIOD_S;
    h.count = (uint32_t)n;
    h.sum = file_sum(h, snap, (size_t)n);

    const char *tmp = FC_HIST_PATH ".tmp";
    FILE *f = fopen(tmp, "wb");
    int rc = -1;
    if (f) {
        /* a short write sets the error flag, which fc_file_commit checks */
        fwrite(&h, sizeof h, 1, f);
        fwrite(snap, sizeof *snap, (size_t)n, f);
        rc = fc_file_commit(f, tmp, FC_HIST_PATH);
    }
    if (rc != 0) {
        if (!s_save_failed)
            fc_log("hist: saving %s failed", FC_HIST_PATH);
        s_save_failed = 1;
        return -1;
    }
    if (s_save_failed)
        fc_log("hist: saving works again");
    s_save_failed = 0;
    s_saved_slot = slot;
    return 0;
}

int fc_hist_save(void) {
    pthread_mutex_lock(&s_save_lock);
    int rc = save_locked();
    pthread_mutex_unlock(&s_save_lock);
    return rc;
}

/* The periodic save runs here, not on the main thread, so a SIGTERM never
 * waits for a write before the fan is handed back. g_running is rechecked
 * under s_save_lock: after the signal this thread starts no save, and one
 * already under way finishes before main's exit save begins. */
static void *saver_thread(void *arg) {
    (void)arg;
    while (g_running) {
        for (int s = 0; s < FC_HIST_SAVE_S && g_running; s++)
            sleep(1);
        pthread_mutex_lock(&s_save_lock);
        if (g_running)
            save_locked();
        pthread_mutex_unlock(&s_save_lock);
    }
    return NULL;
}

int fc_hist_start_saver(void) {
    pthread_t t;
    if (pthread_create(&t, NULL, saver_thread, NULL) != 0) {
        fc_log("hist: saver thread failed, saving on exit only");
        return -1;
    }
    pthread_detach(t);
    return 0;
}

int fc_hist_load(void) {
    static sample buf[FC_HIST_CAP];
    hist_file_hdr h;
    char extra;
    FILE *f = fopen(FC_HIST_PATH, "rb");
    if (!f)
        return 0;
    int ok = fread(&h, sizeof h, 1, f) == 1 &&
             !memcmp(h.magic, HIST_MAGIC, 4) && h.version == HIST_FILE_VER &&
             h.period_s == FC_HIST_PERIOD_S && h.count > 0 &&
             h.count <= FC_HIST_CAP;
    int n = ok ? (int)h.count : 0;
    /* Exact length: a torn or padded file counts as damaged too. */
    if (ok)
        ok = fread(buf, sizeof *buf, (size_t)n, f) == (size_t)n &&
             fread(&extra, 1, 1, f) == 0 &&
             file_sum(h, buf, (size_t)n) == h.sum;
    fclose(f);
    if (!ok) {
        fc_log("hist: %s is damaged, starting empty", FC_HIST_PATH);
        return 0;
    }

    /* Time since the newest saved sample: the larger of the wall clock and
     * the monotonic delta. The monotonic clock can't be set, so within one
     * boot it is a floor even when the wall clock was moved back; after a
     * reboot it restarts near zero and the wall clock wins. Exact for the
     * common case, a relaunch in the same boot with no clock step. Known
     * edge cases, cosmetic and bounded by the span:
     * - a forward wall clock step between save and load (Settings, an NTP
     *   sync by another payload) overstates the gap by the step, also
     *   within one boot;
     * - a reboot with the wall clock set back, once the new uptime passes
     *   the saved monotonic time, understates the gap and draws the old
     *   samples too recent (host test: 900 s restored for a real 7200 s);
     * - a reboot with the clock set back and a shorter uptime reads as a
     *   file from the future and is discarded.
     * A per-boot identifier would fix the reboot cases; none is known. */
    int64_t rt, mono;
    now_s(&rt, &mono);
    int64_t gap = rt - h.newest_rt;
    if (mono - h.newest_mono > gap)
        gap = mono - h.newest_mono;
    if (gap < 0 || gap >= FC_HIST_SPAN_S) {
        fc_log("hist: saved history %s (%llds), not restored",
               gap < 0 ? "is from the future" : "is too old", (long long)gap);
        return 0;
    }

    pthread_mutex_lock(&s_lock);
    s_head = 0;
    s_count = 0;
    for (int i = 0; i < n; i++)
        push_locked(buf[i]);
    /* The newest restored entry stands for the slot before now, so the
     * first live sample lands right after it. */
    push_gap_locked(gap / FC_HIST_PERIOD_S - 1);
    s_last_slot = mono / FC_HIST_PERIOD_S - 1;
    s_last_rt = rt - FC_HIST_PERIOD_S;
    s_last_mono = mono - FC_HIST_PERIOD_S;
    s_saved_slot = -1;
    int kept = s_count;
    pthread_mutex_unlock(&s_lock);

    fc_log("hist: restored %d samples, %llds gap", n, (long long)gap);
    return kept;
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
