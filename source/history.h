#pragma once

#include <stddef.h>

/* Telemetry history for the web panel graph: one sample per FC_HIST_PERIOD_S
 * of CLOCK_MONOTONIC time, the last FC_HIST_SPAN_S seconds kept. Saved to
 * FC_HIST_PATH every FC_HIST_SAVE_S and on exit, restored on start. Time
 * Breeze was not running is filled with gap samples, so every slot is one
 * period and the newest sample is "now". */
#define FC_HIST_PERIOD_S 2
#define FC_HIST_SPAN_S (24 * 60 * 60)
#define FC_HIST_CAP (FC_HIST_SPAN_S / FC_HIST_PERIOD_S)
#define FC_HIST_SAVE_S 300 /* up to 130 KB per save, ~37 MB a day of writes */

/* Stored for slots with no sample (Breeze not running, or the loop stalled).
 * fc_hist_json leaves them out like any negative value. */
#define FC_HIST_GAP (-2)

/* Values are -1 when unavailable (want is -1 while the curve is off).
 * Called once per control tick; keeps the first call in each period. */
void fc_hist_record(int cpu_c, int duty_pct, int want_pct);

/* Restores FC_HIST_PATH into the empty ring. Call once, before the control
 * thread and the saver start. Returns the samples in the ring afterwards
 * (0 = none). */
int fc_hist_load(void);

/* Atomic snapshot to FC_HIST_PATH; skipped when nothing new was recorded.
 * Returns 0 on success or skip, -1 on a failed write (the previous file
 * stays). Any thread: saves are serialized. */
int fc_hist_save(void);

/* Detached thread that calls fc_hist_save every FC_HIST_SAVE_S until
 * g_running is cleared. The exit save stays with main. 0 ok, -1 failed. */
int fc_hist_start_saver(void);

/* Writes the last `seconds` of history as JSON, averaged into at most
 * `max_points` buckets, oldest first:
 *   {"step":<seconds per bucket>,"d":[[cpu,duty,want],...]}
 * A value with no sample in its bucket (gaps included) is -1.
 * Returns the number of bytes written (always NUL-terminated). */
size_t fc_hist_json(char *buf, size_t cap, int seconds, int max_points);
