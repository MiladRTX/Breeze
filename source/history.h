#pragma once

#include <stddef.h>

/* In-memory telemetry history for the web panel graph: one sample every
 * FC_HIST_PERIOD_S seconds, the last FC_HIST_SPAN_S seconds kept. Lost when
 * the plugin restarts. */
#define FC_HIST_PERIOD_S 2
#define FC_HIST_SPAN_S (3 * 60 * 60)
#define FC_HIST_CAP (FC_HIST_SPAN_S / FC_HIST_PERIOD_S)

/* Values are -1 when unavailable (want is -1 while the curve is off). */
void fc_hist_record(int cpu_c, int duty_pct, int want_pct);

/* Writes the last `seconds` of history as JSON, averaged into at most
 * `max_points` buckets, oldest first:
 *   {"step":<seconds per bucket>,"d":[[cpu,duty,want],...]}
 * Returns the number of bytes written (always NUL-terminated). */
size_t fc_hist_json(char *buf, size_t cap, int seconds, int max_points);
