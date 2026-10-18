#pragma once

#include <stddef.h>

/* Makes sure the "Breeze" home screen tile exists and points at the panel
 * on `port`. When it is missing or out of date, hands the embedded
 * breeze-tile.elf to the local ELF loader, which installs it in a separate
 * process (the firmware installer can block, so it never runs in Breeze).
 * Returns 1 if the installer was started, 0 if the tile is current, -1 on
 * failure. */
int fc_launcher_ensure(int port);

/* Starts the embedded breeze-guard.elf unless one is already waiting on
 * breeze.lock (see source/guard.c). Cheap when it is: one LOCK_NB probe. */
void fc_guard_ensure(void);

/* That probe alone: 1 a guard is waiting (it holds guard.lock), 0 none,
 * -1 guard.lock can't be opened (a guard couldn't take it either). */
int fc_guard_probe(void);

/* Sends an ELF to the loader on this console (127.0.0.1:9021), as Payload
 * Manager does. 0 ok, -1 failure. */
int fc_loader_send(const char *elf, size_t len);
