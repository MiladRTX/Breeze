/*
 * Breeze app - talking to breeze.elf, the background payload that runs the fan,
 * through the same HTTP API as the web panel (on 127.0.0.1).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace breeze::link
{
constexpr int max_points = 30; // FC_CURVE_MAX_POINTS
constexpr int max_presets = 10;
constexpr int max_history = 512;

struct Curve
{
    int count;
    int temp[max_points];
    int fan[max_points];
};

// What /api/state reports. Numbers are -1 when breeze.elf has no value.
struct State
{
    char version[16];
    int enabled;     // custom curve on
    int cpu;         // C
    int duty;        // fan %
    int desired;     // % the curve wants
    int target_c100; // fan target in 1/100 C
    int stock_c100;  // the console's own target
    int console_min; // %
    int at_floor;    // the fan sits at the console's minimum
    Curve curve;     // the running curve
};

struct History
{
    int step_s;
    int count;
    signed char temp[max_history]; // -1: no sample
    signed char fan[max_history];
    signed char want[max_history];
};

struct Preset
{
    char name[40];
    Curve curve;
};

struct Presets
{
    int count;
    Preset list[max_presets];
};

enum class Reply
{
    ok,
    no_server, // nothing listens: breeze.elf isn't running (or its web panel is off)
    bad,       // something answered, but not as expected
};

// The panel port from /data/breeze/breeze.conf, 2878 when it can't be read.
[[nodiscard]] int panel_port() noexcept;
// Picks up the write token (breeze.conf, or the panel page). Call before posting.
bool load_token(int port) noexcept;

[[nodiscard]] Reply fetch_state(int port, State &out) noexcept;
[[nodiscard]] Reply fetch_history(int port, int seconds, History &out) noexcept;
[[nodiscard]] Reply fetch_presets(int port, Presets &out) noexcept;

// POST /api/config: the curve on or off, and when curve is given its points.
// Returns the HTTP status (0 when nothing answered).
int post_config(int port, int enabled, const Curve *curve) noexcept;
// POST /api/presets action=apply|save|delete for the named preset. save stores
// curve (the running one when null) and replaces a preset of that name only
// with overwrite. Returns the HTTP status (409: name taken or list full).
int post_preset(int port, const char *action, const char *name, const Curve *curve = nullptr,
                bool overwrite = false) noexcept;

// The console's address on the network, for the web panel's link; false when unknown.
bool local_ip(char *out, int cap) noexcept;

// Sends the breeze.elf packaged with the app to the ELF loader on 127.0.0.1:9021,
// which starts it as its own process: it keeps running after the app closes.
// 0 sent, -1 the packaged copy is missing, -2 no loader answered.
[[nodiscard]] int start_breeze() noexcept;
} // namespace breeze::link
