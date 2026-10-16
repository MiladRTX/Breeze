#include "preset.h"
#include "app.h"

#include <stdio.h>
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
    static const curve_point quiet[] = {
        {58, 20}, {66, 35}, {73, 55}, {79, 75}, {85, 95}};
    static const curve_point balanced[] = {
        {55, 25}, {63, 35}, {70, 50}, {76, 70}, {82, 92}};
    static const curve_point cool[] = {
        {50, 30}, {58, 45}, {65, 62}, {72, 80}, {78, 100}};
    add_default(ps, "Quiet", quiet, 5);
    add_default(ps, "Balanced", balanced, 5);
    add_default(ps, "Cool", cool, 5);
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
    return 0;
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
    fflush(f);
    fclose(f);
    return rename(tmp, FC_PRESETS_PATH);
}

int fc_presets_find(const preset_store *ps, const char *name) {
    for (int i = 0; i < ps->count; i++) {
        if (!strcmp(ps->list[i].name, name))
            return i;
    }
    return -1;
}
