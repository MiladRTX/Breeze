/*
 * Breeze app - the DualSense of the user who opened the app.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace breeze::pad
{
enum Button : unsigned
{
    up = 1U << 0,
    down = 1U << 1,
    left = 1U << 2,
    right = 1U << 3,
    cross = 1U << 4,
    circle = 1U << 5,
    square = 1U << 6,
    triangle = 1U << 7,
    l1 = 1U << 8,
    r1 = 1U << 9,
    l2 = 1U << 10,
    r2 = 1U << 11,
    options = 1U << 12,
    l3 = 1U << 13, // the left stick pressed in
    r3 = 1U << 14,
};

// The first finger on the touchpad: where (0..1 across, 0..1 down). down is
// false with no finger, or while the system has the controller.
struct Touch
{
    bool down;
    float x, y;
};
[[nodiscard]] Touch touch() noexcept;

// False when no controller could be opened.
bool init() noexcept;

// The buttons pressed since the last call. Held directions (D-pad or left
// stick), L1/R1 and L2/R2 repeat, faster after a while. Nothing while the
// system has the controller (the PS button's menu).
[[nodiscard]] unsigned poll() noexcept;
} // namespace breeze::pad
