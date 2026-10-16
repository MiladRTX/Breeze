#pragma once

#include <signal.h>

#include "app.h"

/* Cleared by the signal handler; every loop polls it. */
extern volatile sig_atomic_t g_running;

/* Closed-loop fan controller thread (arg: app_state *). Runs until
 * g_running is cleared. */
void *fc_control_loop(void *arg);
