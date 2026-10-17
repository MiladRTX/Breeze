#pragma once

/* Makes sure the "Breeze" home screen tile exists and points at the panel
 * on `port`. When it is missing or out of date, hands the embedded
 * breeze-tile.elf to the local ELF loader, which installs it in a separate
 * process (the firmware installer can block, so it never runs in Breeze).
 * Returns 1 if the installer was started, 0 if the tile is current, -1 on
 * failure. */
int fc_launcher_ensure(int port);
