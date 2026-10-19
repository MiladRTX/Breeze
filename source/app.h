#pragma once

#include <pthread.h>
#include <stddef.h>

#include "curve.h"
#include "fan.h"
#include "preset.h"

#define FC_PLUGIN_ID "Breeze"
#define FC_PLUGIN_VERSION "1.9"
#define FC_PLUGIN_NAME "Breeze"

typedef struct app_state {
    pthread_mutex_t lock;
    curve_config cfg;
    preset_store presets;
    fan_sample sample;
    int current_setpoint; /* -1 when not holding a custom setpoint */
    int ui_dirty;         /* set by out-of-process changes (web) */
    int stopping;         /* fan handed back; the control loop must not write */
    int console_min;      /* console's own minimum fan %, measured; -1 unknown */
    fan_channel ch[FAN_CHANNELS]; /* servo channels, read every 2 s */
    int last_c256;        /* Breeze's last fan write still in place, -1 none */
} app_state;

void app_state_init(app_state *st);
void app_lock(app_state *st);
void app_unlock(app_state *st);

/* Hand the fan back: if channel 0 still holds a target of Breeze's (either
 * one in breeze.lock), put the stock target back; if the firmware already
 * replaced it, leave it. Returns 1 restored, 0 the fan was not Breeze's, -1
 * failed (the record stays, for a retry). Call with st->lock held. */
int fc_fan_restore_locked(app_state *st);

void fc_log_open(void);
void fc_log_close(void);
void fc_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Call once breeze.lock is held: only then may this copy rotate breeze.log
 * (a starting copy that has not taken over never does). */
void fc_log_own(void);
