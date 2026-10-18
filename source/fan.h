#pragma once

typedef struct fan_sample {
    int cpu_c;    /* -1 when unavailable */
    int soc_c;    /* SoC sensor, the one OnionHEN labels "GPU" */
    int duty_pct; /* 0..100, -1 when unavailable */
} fan_sample;

void fan_read(fan_sample *out);

/* Write the ICC fan setpoint ("hold this temperature"), clamped 0..100.
 * Returns 0 on success, -1 on failure. */
int fan_write_setpoint(int celsius_c);

/* Same, in 1/256 C: the ICC field is 8.8 fixed point (byte 4 = fraction,
 * byte 5 = whole degrees), measured on hardware. */
int fan_write_setpoint_fine(int c256);
