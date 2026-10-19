#pragma once

#include "app.h"

/* Starts the update check on its own thread (see update.c). */
void fc_update_start(app_state *st);
