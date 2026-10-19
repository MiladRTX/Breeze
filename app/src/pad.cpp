/*
 * Breeze app - the DualSense of the user who opened the app.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pad.hpp"
#include "screen.hpp"

#include <cstdint>

extern "C"
{
    int sceUserServiceInitialize(const void *params);
    int sceUserServiceGetInitialUser(std::int32_t *user_id);
    int scePadInit();
    int scePadOpen(std::int32_t user_id, std::int32_t type, std::int32_t index,
                   const void *param);
    int scePadGetHandle(std::int32_t user_id, std::int32_t type, std::int32_t index);
    int scePadRead(std::int32_t handle, void *data, std::int32_t count);
    int scePadGetControllerInformation(std::int32_t handle, void *info);
}

namespace breeze::pad
{
namespace
{
// One ScePadData sample: buttons at 0, sticks at 4 (left x, y, right x, y),
// touch data at 52 (finger count; the first finger's x and y as u16 at 60, 62).
constexpr int sample_size = 120;
constexpr int touch_count_at = 52, touch_x_at = 60, touch_y_at = 62;
constexpr int sample_count = 32;
constexpr std::uint32_t intercepted = UINT32_C(0x80000000);

constexpr std::uint32_t raw_up = 0x0010, raw_right = 0x0020, raw_down = 0x0040,
                        raw_left = 0x0080, raw_l2 = 0x0100, raw_r2 = 0x0200, raw_l1 = 0x0400,
                        raw_r1 = 0x0800, raw_triangle = 0x1000, raw_circle = 0x2000,
                        raw_cross = 0x4000, raw_square = 0x8000, raw_options = 0x0008,
                        raw_l3 = 0x0002, raw_r3 = 0x0004;

constexpr unsigned repeating = up | down | left | right | l1 | r1 | l2 | r2;
constexpr std::uint64_t repeat_delay_ms = 350, repeat_ms = 110, fast_after_ms = 1500,
                        fast_repeat_ms = 40;

int g_handle = -1;
alignas(16) unsigned char g_samples[sample_count][sample_size];
unsigned g_held = 0;
Touch g_touch{};
// The touchpad's resolution (from the controller, else the DualSense's), grown
// to the largest coordinate seen so the far edges always reach 1.
float g_touch_w = 1920, g_touch_h = 1080;
unsigned g_repeat_button = 0;
std::uint64_t g_held_since = 0, g_next_repeat = 0;

unsigned translate(std::uint32_t raw, std::uint8_t lx, std::uint8_t ly) noexcept
{
    unsigned b = 0;
    if (raw & raw_up)
        b |= up;
    if (raw & raw_down)
        b |= down;
    if (raw & raw_left)
        b |= left;
    if (raw & raw_right)
        b |= right;
    if (raw & raw_cross)
        b |= cross;
    if (raw & raw_circle)
        b |= circle;
    if (raw & raw_square)
        b |= square;
    if (raw & raw_triangle)
        b |= triangle;
    if (raw & raw_l1)
        b |= l1;
    if (raw & raw_r1)
        b |= r1;
    if (raw & raw_l2)
        b |= l2;
    if (raw & raw_r2)
        b |= r2;
    if (raw & raw_options)
        b |= options;
    if (raw & raw_l3)
        b |= l3;
    if (raw & raw_r3)
        b |= r3;
    // The left stick as a D-pad, along its stronger axis.
    const int x = lx - 128, y = ly - 128;
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax >= 80 && ax > ay)
        b |= x < 0 ? left : right;
    else if (ay >= 80)
        b |= y < 0 ? up : down;
    return b;
}
} // namespace

bool init() noexcept
{
    (void)sceUserServiceInitialize(nullptr); // refused if already done: harmless
    std::int32_t user = -1;
    if (sceUserServiceGetInitialUser(&user) < 0 || user < 0 || scePadInit() < 0)
        return false;
    g_handle = scePadOpen(user, 0, 0, nullptr);
    if (g_handle < 0)
        g_handle = scePadGetHandle(user, 0, 0);
    if (g_handle >= 0)
    {
        // ScePadControllerInformation: touchpad pixel density (float), then its
        // resolution as two u16.
        alignas(8) unsigned char info[64] = {};
        if (scePadGetControllerInformation(g_handle, info) == 0)
        {
            const unsigned rx = info[4] | info[5] << 8, ry = info[6] | info[7] << 8;
            if (rx > 100 && ry > 100)
            {
                g_touch_w = static_cast<float>(rx);
                g_touch_h = static_cast<float>(ry);
            }
        }
    }
    return g_handle >= 0;
}

unsigned poll() noexcept
{
    if (g_handle < 0)
        return 0;
    const int count = scePadRead(g_handle, g_samples, sample_count);
    unsigned pressed = 0;
    for (int i = 0; i < count; ++i)
    {
        const unsigned char *s = g_samples[i];
        const std::uint32_t raw = static_cast<std::uint32_t>(s[0]) |
                                  static_cast<std::uint32_t>(s[1]) << 8 |
                                  static_cast<std::uint32_t>(s[2]) << 16 |
                                  static_cast<std::uint32_t>(s[3]) << 24;
        const unsigned now = (raw & intercepted) ? 0 : translate(raw, s[4], s[5]);
        pressed |= now & ~g_held;
        g_held = now;

        g_touch.down = !(raw & intercepted) && s[touch_count_at] > 0;
        if (g_touch.down)
        {
            const float x = static_cast<float>(s[touch_x_at] | s[touch_x_at + 1] << 8);
            const float y = static_cast<float>(s[touch_y_at] | s[touch_y_at + 1] << 8);
            if (x + 1 > g_touch_w)
                g_touch_w = x + 1;
            if (y + 1 > g_touch_h)
                g_touch_h = y + 1;
            g_touch.x = x / (g_touch_w - 1);
            g_touch.y = y / (g_touch_h - 1);
        }
    }

    // Repeat the newest held repeating button.
    const std::uint64_t t = screen::now_ms();
    const unsigned fresh = pressed & repeating;
    if (fresh)
    {
        g_repeat_button = fresh & (~fresh + 1U); // its lowest bit
        g_held_since = t;
        g_next_repeat = t + repeat_delay_ms;
    }
    else if (g_repeat_button && (g_held & g_repeat_button))
    {
        if (t >= g_next_repeat)
        {
            pressed |= g_repeat_button;
            g_next_repeat = t + (t - g_held_since >= fast_after_ms ? fast_repeat_ms : repeat_ms);
        }
    }
    else
        g_repeat_button = 0;
    return pressed;
}
Touch touch() noexcept
{
    return g_touch;
}
} // namespace breeze::pad
