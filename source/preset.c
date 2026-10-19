#include "preset.h"
#include "app.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fc_preset_name_clean(char *dst, const char *src, size_t dst_size) {
    if (dst_size == 0)
        return;

    size_t w = 0;
    for (const char *p = src; *p && w + 1 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126 || c == '=' || c == '"' || c == '\\')
            continue;
        dst[w++] = (char)c;
    }
    dst[w] = 0;

    char *s = dst;
    while (*s == ' ')
        s++;
    char *e = s + strlen(s);
    while (e > s && e[-1] == ' ')
        e--;
    *e = 0;
    if (s != dst)
        memmove(dst, s, (size_t)(e - s) + 1);
}

static void add_default(preset_store *ps, const char *name,
                        const curve_point *pts, int n) {
    if (ps->count >= FC_PRESET_MAX)
        return;
    preset *p = &ps->list[ps->count++];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    p->points_count = n;
    for (int i = 0; i < n && i < FC_CURVE_MAX_POINTS; i++)
        p->points[i] = pts[i];
    fc_sanitize_points(&p->points_count, p->points);
}

void fc_presets_defaults(preset_store *ps) {
    memset(ps, 0, sizeof *ps);
    /* One starting preset: the default curve, play-tested on hardware. */
    curve_config def;
    fc_defaults(&def);
    add_default(ps, "Breeze", def.points, def.points_count);
}

int fc_presets_load(preset_store *ps) {
    FILE *f = fopen(FC_PRESETS_PATH, "r");
    if (!f) {
        fc_presets_defaults(ps);
        return fc_presets_save(ps);
    }

    memset(ps, 0, sizeof *ps);
    preset *cur = NULL;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\0')
            continue;
        if (!strncmp(p, "preset=", 7)) {
            if (cur && cur->points_count == 0)
                ps->count--;
            if (ps->count >= FC_PRESET_MAX) {
                cur = NULL;
                continue;
            }
            cur = &ps->list[ps->count++];
            memset(cur, 0, sizeof *cur);
            char raw[FC_PRESET_NAME_MAX * 2 + 1];
            snprintf(raw, sizeof raw, "%s", p + 7);
            char *nl = strpbrk(raw, "\r\n");
            if (nl)
                *nl = 0;
            fc_preset_name_clean(cur->name, raw, sizeof cur->name);
        } else if (!strncmp(p, "point=", 6) && cur &&
                   cur->points_count < FC_CURVE_MAX_POINTS) {
            int t = 0, v = 0;
            if (sscanf(p + 6, "%d,%d", &t, &v) == 2) {
                cur->points[cur->points_count].temp_c = t;
                cur->points[cur->points_count].fan_pct = v;
                cur->points_count++;
            }
        }
    }
    if (cur && cur->points_count == 0)
        ps->count--;
    fclose(f);

    int w = 0;
    for (int i = 0; i < ps->count; i++) {
        if (ps->list[i].name[0] == 0 || ps->list[i].points_count == 0)
            continue;
        fc_sanitize_points(&ps->list[i].points_count, ps->list[i].points);
        if (w != i)
            ps->list[w] = ps->list[i];
        w++;
    }
    ps->count = w;

    /* Deleted in an older version: the built-in preset is always there. */
    if (fc_presets_find(ps, FC_PRESET_BUILTIN) < 0 && ps->count < FC_PRESET_MAX) {
        preset_store defaults;
        fc_presets_defaults(&defaults);
        memmove(&ps->list[1], &ps->list[0], sizeof ps->list[0] * (size_t)ps->count);
        ps->list[0] = defaults.list[0];
        ps->count++;
        return fc_presets_save(ps);
    }
    return 0;
}

int fc_preset_is_builtin(const char *name) {
    return !strcmp(name, FC_PRESET_BUILTIN);
}

void fc_presets_new_name(const preset_store *ps, char *out, size_t n) {
    for (int k = 1;; k++) {
        if (k == 1)
            snprintf(out, n, "My curve");
        else
            snprintf(out, n, "My curve %d", k);
        if (fc_presets_find(ps, out) < 0)
            return;
    }
}

int fc_presets_store_edit(preset_store *ps, const char *target, const curve_point *pts, int n,
                          char *saved_as, size_t saved_n) {
    int idx = target[0] && !fc_preset_is_builtin(target) ? fc_presets_find(ps, target) : -1;
    if (idx < 0) {
        if (ps->count >= FC_PRESET_MAX) {
            saved_as[0] = 0;
            return 0;
        }
        /* The name first: fc_presets_new_name looks it up in the store, which
         * must not hold it yet. */
        char name[FC_PRESET_NAME_MAX + 1];
        fc_presets_new_name(ps, name, sizeof name);
        idx = ps->count++;
        memset(&ps->list[idx], 0, sizeof ps->list[idx]);
        snprintf(ps->list[idx].name, sizeof ps->list[idx].name, "%s", name);
    }
    preset *p = &ps->list[idx];
    p->points_count = n;
    memcpy(p->points, pts, sizeof p->points[0] * (size_t)n);
    fc_sanitize_points(&p->points_count, p->points);
    snprintf(saved_as, saved_n, "%s", p->name);
    return 1;
}

static int same_points(const preset *p, const curve_config *c) {
    if (p->points_count != c->points_count)
        return 0;
    for (int i = 0; i < p->points_count; i++)
        if (p->points[i].temp_c != c->points[i].temp_c ||
            p->points[i].fan_pct != c->points[i].fan_pct)
            return 0;
    return 1;
}

void fc_presets_fix_active(const preset_store *ps, curve_config *c) {
    if (c->preset[0]) {
        if (fc_presets_find(ps, c->preset) < 0)
            c->preset[0] = 0;
        return;
    }
    int builtin = -1;
    for (int i = 0; i < ps->count; i++) {
        if (!same_points(&ps->list[i], c))
            continue;
        if (!fc_preset_is_builtin(ps->list[i].name)) {
            snprintf(c->preset, sizeof c->preset, "%s", ps->list[i].name);
            return;
        }
        builtin = i;
    }
    if (builtin >= 0)
        snprintf(c->preset, sizeof c->preset, "%s", ps->list[builtin].name);
}

int fc_presets_save(const preset_store *ps) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", FC_PRESETS_PATH);

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    fprintf(f, "# %s fan curve presets\n", FC_PLUGIN_ID);
    fprintf(f, "# preset=<name> followed by point=tempC,fan%% lines\n");
    for (int i = 0; i < ps->count; i++) {
        fprintf(f, "preset=%s\n", ps->list[i].name);
        for (int j = 0; j < ps->list[i].points_count; j++)
            fprintf(f, "point=%d,%d\n", ps->list[i].points[j].temp_c,
                    ps->list[i].points[j].fan_pct);
    }
    return fc_file_commit(f, tmp, FC_PRESETS_PATH);
}

int fc_presets_find(const preset_store *ps, const char *name) {
    for (int i = 0; i < ps->count; i++) {
        if (!strcmp(ps->list[i].name, name))
            return i;
    }
    return -1;
}
