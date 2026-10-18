#include "curve.h"

#include <stdint.h>
#include <string.h>

void fc_defaults(curve_config *c) {
    memset(c, 0, sizeof *c);
    /* Off until the user switches it on once; that choice is saved and
     * kept on every launch after. */
    c->enabled = 0;
    c->web_enabled = 1;
    c->web_port = FC_DEFAULT_WEB_PORT;
    /* Also the "Breeze" starting preset: the curve play-tested on hardware,
     * so switching on is all a first start needs. It sits on the console's
     * in-game minimum (23%) up to 60 C and climbs to 40% by 80 C. */
    static const curve_point def[] = {
        {50, 13}, {55, 23}, {60, 23}, {70, 26},
        {75, 32}, {78, 37}, {80, 40}};
    c->points_count = (int)(sizeof def / sizeof def[0]);
    memcpy(c->points, def, sizeof def);
}

void fc_sanitize_points(int *count, curve_point *points) {
    int n = *count;
    if (n < 1)
        n = 1;
    if (n > FC_CURVE_MAX_POINTS)
        n = FC_CURVE_MAX_POINTS;

    for (int i = 0; i < n; i++) {
        if (points[i].temp_c < FC_TEMP_MIN_C)
            points[i].temp_c = FC_TEMP_MIN_C;
        if (points[i].temp_c > FC_TEMP_MAX_C)
            points[i].temp_c = FC_TEMP_MAX_C;
        if (points[i].fan_pct < FC_FAN_MIN_PCT)
            points[i].fan_pct = FC_FAN_MIN_PCT;
        if (points[i].fan_pct > 100)
            points[i].fan_pct = 100;
    }

    for (int i = 1; i < n; i++) {
        curve_point key = points[i];
        int j = i - 1;
        while (j >= 0 && points[j].temp_c > key.temp_c) {
            points[j + 1] = points[j];
            j--;
        }
        points[j + 1] = key;
    }

    int w = 0;
    for (int i = 0; i < n; i++) {
        if (w > 0 && points[w - 1].temp_c == points[i].temp_c) {
            if (points[i].fan_pct > points[w - 1].fan_pct)
                points[w - 1].fan_pct = points[i].fan_pct;
        } else {
            points[w++] = points[i];
        }
    }
    *count = w;
}

void fc_sanitize(curve_config *c) {
    c->enabled = c->enabled ? 1 : 0;
    c->web_enabled = c->web_enabled ? 1 : 0;
    c->smooth = c->smooth ? 1 : 0;
    if (c->web_port < 1024 || c->web_port > 65535)
        c->web_port = FC_DEFAULT_WEB_PORT;
    fc_sanitize_points(&c->points_count, c->points);
}

/* Smooth tangents are fixed point: 1/65536 % per degree C. */
#define FC_SMOOTH_ONE ((int64_t)1 << 16)

/* Tangent at point i minus the slope of segment k (i or i-1), in
 * FC_SMOOTH_ONE units, truncated toward zero so the tangent used always lies
 * between the exact one and that segment's slope. */
static int64_t tan_dev(const curve_point *p, int n, int i, int k) {
    int hk = p[k + 1].temp_c - p[k].temp_c;
    int dk = p[k + 1].fan_pct - p[k].fan_pct;
    if (i <= 0 || i >= n - 1)
        return 0; /* end point: the tangent is its segment's slope */
    int h1 = p[i].temp_c - p[i - 1].temp_c;
    int d1 = p[i].fan_pct - p[i - 1].fan_pct;
    int h2 = p[i + 1].temp_c - p[i].temp_c;
    int d2 = p[i + 1].fan_pct - p[i].fan_pct;
    if (h1 <= 0 || h2 <= 0 || hk <= 0)
        return 0;
    if (d1 * d2 <= 0) /* peak, valley or flat neighbour: tangent 0 */
        return -(int64_t)dk * FC_SMOOTH_ONE / hk;
    /* Weighted harmonic mean of the two slopes (Fritsch-Butland):
     * m = (w1 + w2) / (w1 / s1 + w2 / s2), w1 = 2*h2 + h1, w2 = h2 + 2*h1,
     * which is a / b with these integers. */
    int64_t a = 3 * (int64_t)(h1 + h2) * d1 * d2;
    int64_t b = (int64_t)(2 * h2 + h1) * h1 * d2 +
                (int64_t)(h2 + 2 * h1) * h2 * d1;
    if (b < 0) {
        a = -a;
        b = -b;
    }
    /* m - dk/hk = (a*hk - dk*b) / (b*hk) */
    return (a * hk - (int64_t)dk * b) * FC_SMOOTH_ONE / (b * hk);
}

int fc_eval_points(const curve_point *p, int n, int smooth, int temp_c) {
    if (n < 1)
        return 0;
    if (temp_c <= p[0].temp_c)
        return p[0].fan_pct;
    for (int k = 0; k + 1 < n; k++) {
        if (temp_c > p[k + 1].temp_c)
            continue;
        int h = p[k + 1].temp_c - p[k].temp_c;
        int d = p[k + 1].fan_pct - p[k].fan_pct;
        int t = temp_c - p[k].temp_c;
        if (h <= 0)
            return p[k + 1].fan_pct;
        if (!smooth)
            return p[k].fan_pct + (int)(((int64_t)t * d) / h);
        int u = h - t;
        int64_t e = tan_dev(p, n, k, k);
        int64_t f = tan_dev(p, n, k + 1, k);
        /* Hermite cubic = line + t*u*(e*u - f*t) / (h^2 * ONE) */
        int64_t num = (int64_t)d * t * h * FC_SMOOTH_ONE +
                      (int64_t)t * u * (e * u - f * t);
        return p[k].fan_pct + (int)(num / ((int64_t)h * h * FC_SMOOTH_ONE));
    }
    return p[n - 1].fan_pct;
}

int fc_evaluate(const curve_config *c, int temp_c) {
    return fc_eval_points(c->points, c->points_count, c->smooth, temp_c);
}
