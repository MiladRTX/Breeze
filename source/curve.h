#pragma once

#define FC_CURVE_MAX_POINTS 30
#define FC_TEMP_MIN_C 30
#define FC_TEMP_MAX_C 100
#define FC_DEFAULT_WEB_PORT 2878

/* ICC target used by the firmware's own curve (OnionHEN calls this the
 * automatic threshold). Writing it restores stock behavior. */
#define FC_STOCK_TARGET_C 77

/* Past this temperature the controller is overridden regardless of points. */
#define FC_SAFETY_TEMP_C 88
#define FC_SAFETY_TARGET_C 33

/* Closed-loop target steering limits */
/* The ICC only adds fan while the temperature is above the target, so the
 * lowest target sets how much fan the curve can ask for at a given temp.
 * 33 C meant no extra fan at all at a 33 C idle; 20 C leaves room. */
#define FC_TARGET_MIN_C 20
#define FC_TARGET_MAX_C 90
#define FC_CTL_DEADBAND_PCT 4    /* fan over the curve by more -> less fan */
#define FC_CTL_DEADBAND_UP_PCT 8 /* fan under the curve by more -> more fan */
/* Ticks (1 s) to hold after a target step before the next one: the ICC's
 * duty response lags a few seconds and the 7-sample median adds ~3 more, so
 * stepping sooner acts on a duty reading that predates the last step. */
#define FC_CTL_COOLDOWN_TICKS 5
/* Fan this far over the curve with the target at/below the temp triggers an
 * immediate jump of the target to just above the temp (see fan_loop). */
#define FC_CTL_ESCAPE_PCT 12
/* Raw temp this far under the EMA counts as a dip (loading screens drop the
 * CPU ~10-15 C for a second or two). After it ends, steps that lower the
 * target wait this many ticks for the ICC duty to recover on its own. */
#define FC_DIP_C 5
#define FC_DIP_HOLD_TICKS 10
/* Raw temp this far over the EMA counts as a fast rise (rise gate). */
#define FC_RISE_C 3

/* Web panel write token: hex chars, without the terminator. */
#define FC_TOKEN_LEN 32

typedef struct curve_point {
    int temp_c;  /* boundary */
    int fan_pct; /* desired fan duty at/above this boundary */
} curve_point;

typedef struct curve_config {
    int enabled;     /* custom curve active */
    int web_enabled; /* embedded web panel */
    int web_port;
    int points_count;
    curve_point points[FC_CURVE_MAX_POINTS]; /* ascending by temp after sanitize */
    char token[FC_TOKEN_LEN + 1]; /* required on web panel POSTs */
} curve_config;

void fc_defaults(curve_config *c);

/* Clamp every field, sort points ascending, collapse duplicate boundaries. */
void fc_sanitize(curve_config *c);

/* Same point-level clamps for a bare count/array (used for presets). */
void fc_sanitize_points(int *count, curve_point *points);

/* Desired fan duty for a measured temperature. Linear interpolation between
 * the configured points; clamped to the first/last point outside the range.
 * This matches the line drawn by the web graph. */
int fc_evaluate(const curve_config *c, int temp_c);
