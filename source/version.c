#include "version.h"

#include <string.h>

static long next_part(const char **p) {
    long v = 0;
    while (**p >= '0' && **p <= '9') {
        if (v < 100000)
            v = v * 10 + (**p - '0');
        (*p)++;
    }
    if (**p == '.')
        (*p)++;
    return v;
}

int fc_version_cmp(const char *a, const char *b) {
    if (*a == 'v')
        a++;
    if (*b == 'v')
        b++;
    while ((*a >= '0' && *a <= '9') || (*b >= '0' && *b <= '9')) {
        long x = next_part(&a), y = next_part(&b);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

int fc_release_tag(const char *json, char *out, size_t n) {
    const char *p = strstr(json, "\"tag_name\"");
    if (!p || n < 2)
        return -1;
    p += 10;
    while (*p == ' ' || *p == ':')
        p++;
    if (*p++ != '"')
        return -1;
    if (*p == 'v' || *p == 'V')
        p++;
    size_t len = strcspn(p, "\"");
    if (!p[len] || len == 0 || len >= n || *p < '0' || *p > '9')
        return -1;
    for (size_t i = 0; i < len; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || p[i] == '.'))
            return -1;
    memcpy(out, p, len);
    out[len] = 0;
    return 0;
}
