#pragma once

#include "curve.h"

#define FC_DATA_DIR "/data/breeze"
#define FC_CONFIG_PATH FC_DATA_DIR "/breeze.conf"
#define FC_LOG_PATH FC_DATA_DIR "/breeze.log"

/* Fills defaults, overlays the file when present, writes defaults when not,
 * sanitizes the result. Returns 0. */
int fc_config_load(curve_config *c);

/* Atomic write (tmp + rename). Returns 0 on success. */
int fc_config_save(const curve_config *c);
