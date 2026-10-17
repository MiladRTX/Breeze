#include "curve.h"

#include <string.h>

void fc_defaults(curve_config *c) {
    memset(c, 0, sizeof *c);
    /* Off until the user switches it on once; that choice is saved and
     * kept on every launch after. */
    c->enabled = 0;
    c->web_enabled = 1;
    c->web_port = FC_DEFAULT_WEB_PORT;
    /* Also the "Breeze" starting preset: the curve play-tested on hardware,
     * so switching on is all a first start needs. */
    static const curve_point def[] = {
        {50, 13}, {55, 15}, {60, 17}, {65, 20}, {70, 24},
        {75, 28}, {79, 32}, {82, 38}, {85, 49}};
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
        if (points[i].fan_pct < 0)
            points[i].fan_pct = 0;
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
    if (c->web_port < 1024 || c->web_port > 65535)
        c->web_port = FC_DEFAULT_WEB_PORT;
    fc_sanitize_points(&c->points_count, c->points);
}

int fc_evaluate(const curve_config *c, int temp_c) {
    const curve_point *p = c->points;
    int n = c->points_count;
    if (n < 1)
        return 0;
    if (temp_c <= p[0].temp_c)
        return p[0].fan_pct;
    for (int i = 1; i < n; i++) {
        if (temp_c <= p[i].temp_c) {
            int dt = p[i].temp_c - p[i - 1].temp_c;
            if (dt <= 0)
                return p[i].fan_pct;
            long v = p[i - 1].fan_pct +
                     ((long)(temp_c - p[i - 1].temp_c) *
                      (p[i].fan_pct - p[i - 1].fan_pct)) /
                         dt;
            return (int)v;
        }
    }
    return p[n - 1].fan_pct;
}
