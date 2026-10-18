#include "config.h"
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int token_valid(const char *t) {
    size_t n = 0;
    for (; t[n]; n++) {
        char ch = t[n];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return 0;
    }
    return n == FC_TOKEN_LEN;
}

static void token_generate(char *out) {
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[FC_TOKEN_LEN / 2];
    arc4random_buf(raw, sizeof raw);
    for (size_t i = 0; i < sizeof raw; i++) {
        out[2 * i] = hex[raw[i] >> 4];
        out[2 * i + 1] = hex[raw[i] & 15];
    }
    out[FC_TOKEN_LEN] = 0;
}

int fc_file_commit(FILE *f, const char *tmp, const char *path) {
    /* Only replace the real file once the new one is fully on disk: a full
     * /data or a failed write must not swap in a short or empty file. */
    int bad = ferror(f);
    if (fflush(f) != 0)
        bad = 1;
    if (fsync(fileno(f)) != 0)
        bad = 1;
    if (fclose(f) != 0)
        bad = 1;
    if (bad || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int fc_config_save(const curve_config *c) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", FC_CONFIG_PATH);

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    fprintf(f, "# %s fan curve config\n", FC_PLUGIN_ID);
    fprintf(f, "# pointN=tempC,fan%% -- the plugin steers the ICC target until\n");
    fprintf(f, "# the measured fan duty matches this curve.\n");
    fprintf(f, "# token= guards web panel writes; delete the line to rotate it.\n");
    fprintf(f, "# smooth=1 bends the curve through the points, 0 = straight lines.\n");
    fprintf(f, "controller=duty\n");
    fprintf(f, "enabled=%d\n", c->enabled);
    fprintf(f, "web_enabled=%d\n", c->web_enabled);
    fprintf(f, "web_port=%d\n", c->web_port);
    fprintf(f, "smooth=%d\n", c->smooth);
    fprintf(f, "token=%s\n", c->token);
    for (int i = 0; i < c->points_count; i++)
        fprintf(f, "point%d=%d,%d\n", i, c->points[i].temp_c,
                c->points[i].fan_pct);
    return fc_file_commit(f, tmp, FC_CONFIG_PATH);
}

int fc_config_load(curve_config *c) {
    fc_defaults(c);

    FILE *f = fopen(FC_CONFIG_PATH, "r");
    if (!f) {
        token_generate(c->token);
        fc_config_save(c);
        return 0;
    }

    int legacy = 1;
    int points_read = 0; /* pointN lines read */
    curve_point file_pts[FC_CURVE_MAX_POINTS];
    unsigned char seen[FC_CURVE_MAX_POINTS] = {0};
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\0')
            continue;
        if (!strncmp(p, "controller=", 11)) {
            if (!strncmp(p + 11, "duty", 4))
                legacy = 0;
        } else if (!strncmp(p, "enabled=", 8)) {
            c->enabled = atoi(p + 8);
        } else if (!strncmp(p, "web_enabled=", 12)) {
            c->web_enabled = atoi(p + 12);
        } else if (!strncmp(p, "web_port=", 9)) {
            c->web_port = atoi(p + 9);
        } else if (!strncmp(p, "smooth=", 7)) {
            c->smooth = atoi(p + 7);
        } else if (!strncmp(p, "token=", 6)) {
            snprintf(c->token, sizeof c->token, "%.*s", FC_TOKEN_LEN,
                     p + 6);
            c->token[strcspn(c->token, "\r\n \t")] = 0;
        } else if (!strncmp(p, "point", 5)) {
            int i = 0, t = 0, v = 0;
            if (sscanf(p, "point%d=%d,%d", &i, &t, &v) == 3 && i >= 0 &&
                i < FC_CURVE_MAX_POINTS) {
                file_pts[i].temp_c = t;
                file_pts[i].fan_pct = v;
                if (!seen[i])
                    points_read++;
                seen[i] = 1;
            }
        }
    }
    fclose(f);

    /* The file's curve replaces the defaults outright (overlaying kept
     * default points past a shorter saved curve). Only the indices actually
     * present count: a gap must not turn into a fake 0/0 point. */
    if (points_read) {
        int n = 0;
        for (int i = 0; i < FC_CURVE_MAX_POINTS; i++)
            if (seen[i])
                c->points[n++] = file_pts[i];
        c->points_count = n;
    }

    /* Pre-controller configs stored ICC temperature targets. Map them onto
     * the observed target -> duty response so old curves keep their intent.
     * Defaults are already duty values, so only file points are converted. */
    if (legacy && points_read) {
        for (int i = 0; i < c->points_count; i++) {
            int v = (90 - c->points[i].fan_pct) * 2;
            if (v < 0)
                v = 0;
            if (v > 100)
                v = 100;
            c->points[i].fan_pct = v;
        }
    }

    int new_token = !token_valid(c->token);
    if (new_token)
        token_generate(c->token);

    fc_sanitize(c);
    if (legacy || new_token)
        (void)fc_config_save(c);
    return 0;
}
