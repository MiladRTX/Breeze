#pragma once

#include <stdio.h>

#include "curve.h"

#define FC_DATA_DIR "/data/breeze"
#define FC_CONFIG_PATH FC_DATA_DIR "/breeze.conf"
#define FC_LOG_PATH FC_DATA_DIR "/breeze.log"
#define FC_HIST_PATH FC_DATA_DIR "/history.bin"

/* Fills defaults, overlays the file when present, writes defaults when not,
 * sanitizes the result. Returns 0. */
int fc_config_load(curve_config *c);

/* Atomic write (tmp + rename). Returns 0 on success. */
int fc_config_save(const curve_config *c);

/* Closes `f` (written to `tmp`) and renames it over `path` only when every
 * byte reached the disk; on any failure removes `tmp` and returns -1. */
int fc_file_commit(FILE *f, const char *tmp, const char *path);
