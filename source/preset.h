#pragma once

#include <stddef.h>

#include "curve.h"

#define FC_PRESET_MAX 10
#define FC_PRESETS_PATH "/data/breeze/presets.conf"
/* The built-in preset (the default curve): never changed, renamed or
 * deleted, and put back when a store lacks it. */
#define FC_PRESET_BUILTIN "Breeze"

typedef struct preset {
    char name[FC_PRESET_NAME_MAX + 1];
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

/* 1 for the built-in preset's name. */
int fc_preset_is_builtin(const char *name);

/* The first free name of "My curve", "My curve 2", ... into out. */
void fc_presets_new_name(const preset_store *ps, char *out, size_t n);

/* Saves an applied curve into the preset `target` (an edit of it): that
 * preset's points are replaced, unless it is the built-in one, missing or
 * "", when the curve becomes a new preset instead. Writes the name it went
 * to into saved_as, "" when the store is full. Returns 1 when the store
 * changed (it then needs saving), 0 otherwise. */
int fc_presets_store_edit(preset_store *ps, const char *target, const curve_point *pts, int n,
                          char *saved_as, size_t saved_n);

/* At start: the active preset for a config from before there was one (the
 * first preset whose points are the running curve, the built-in one last),
 * or "" when the named one is gone. */
void fc_presets_fix_active(const preset_store *ps, curve_config *c);

/* Copies and filters a user-supplied name (printable ASCII, no '=', '"' or
 * '\', trimmed, truncated). */
void fc_preset_name_clean(char *dst, const char *src, size_t dst_size);
