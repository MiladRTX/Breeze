#pragma once

#include <pthread.h>
#include <stddef.h>

#include "curve.h"
#include "fan.h"
#include "preset.h"

#define FC_PLUGIN_ID "Breeze"
#define FC_PLUGIN_VERSION "1.1"
#define FC_PLUGIN_NAME "Breeze"

typedef struct app_state {
    pthread_mutex_t lock;
    curve_config cfg;
    preset_store presets;
    fan_sample sample;
    int current_setpoint; /* -1 when not holding a custom setpoint */
    int ui_dirty;         /* set by out-of-process changes (web) */
} app_state;

void app_state_init(app_state *st);
void app_lock(app_state *st);
void app_unlock(app_state *st);

void fc_log_open(void);
void fc_log_close(void);
void fc_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
