#pragma once

#include <stddef.h>

#include "curve.h"

#define FC_PRESET_MAX 10
#define FC_PRESET_NAME_MAX 40
#define FC_PRESETS_PATH "/data/breeze/presets.conf"

typedef struct preset {
    char name[FC_PRESET_NAME_MAX + 1];
    int smooth; /* as curve_config.smooth */
    int points_count;
    curve_point points[FC_CURVE_MAX_POINTS];
} preset;

typedef struct preset_store {
    int count;
    preset list[FC_PRESET_MAX];
} preset_store;

/* Quiet / Balanced / Cool seed set, sanitized. */
void fc_presets_defaults(preset_store *ps);

/* Loads the store file; seeds defaults and writes the file when missing. */
int fc_presets_load(preset_store *ps);

/* Atomic write (tmp + rename). Returns 0 on success. */
int fc_presets_save(const preset_store *ps);

/* Index of the preset with this exact name, or -1. */
int fc_presets_find(const preset_store *ps, const char *name);

/* Copies and filters a user-supplied name (printable ASCII, no '=', '"' or
 * '\', trimmed, truncated). */
void fc_preset_name_clean(char *dst, const char *src, size_t dst_size);
