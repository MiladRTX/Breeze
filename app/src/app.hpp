/*
 * Breeze app - what the screens share.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "link.hpp"
#include "pad.hpp"
#include "screen.hpp"

#include <cstdint>

namespace breeze::app
{
enum class Tab
{
    curve,
    history,
    presets,
    settings,
};
constexpr int tab_count = 4;

enum class Phase
{
    looking,   // before the first answer
    running,
    starting,  // breeze.elf sent to the loader, waiting for its panel
    stopped,   // was running, then went away
    no_loader, // nothing listens on the ELF loader's port
    no_copy,   // the app's own breeze.elf is missing
    no_answer, // started, but its panel never answered (turned off?)
};

// The kind of the last change to the draft: a run of fan (or temperature)
// nudges to one point is one undo step.
enum class Edit
{
    none,
    fan,
    temp,
    other,
};

struct App
{
    static constexpr int undo_max = 100;

    int port = 2878;
    char ip[20] = "";
    bool have_token = false;
    Phase phase = Phase::looking;
    link::State state{};
    Tab tab = Tab::curve;

    // Curve editor: the draft follows the running curve until it is edited.
    link::Curve draft{};
    bool dirty = false;
    int selected = 0;
    // Undo and redo: drafts before each change and the ones undone, newest
    // last, and the change a run of nudges is adding to.
    link::Curve undo[undo_max]{};
    link::Curve redo[undo_max]{};
    int undo_count = 0, redo_count = 0;
    Edit last_edit = Edit::none;
    int last_point = -1;

    int range = 0; // history range, index into the screens' list
    link::History history{};
    bool history_valid = false;
    std::uint64_t history_due = 0;

    link::Presets presets{};
    bool presets_valid = false;
    int preset_selected = 0;
    // The preset last used, saved or overwritten: the one marked Active when
    // several match the running curve (the web panel's rule).
    char picked[40] = "";

    // Touchpad: the finger's place on the History chart (0..1), and the Curve
    // point it is moving (-1 when no finger is down).
    bool touching = false;
    float touch_x = 0, touch_y = 0;
    int drag = -1;

    int setting_selected = 0;

    // A destructive action waits for a second press of the same button.
    int confirm_what = -1; // screen-specific id, -1 none
    std::uint64_t confirm_until = 0;

    char toast[96] = "";
    bool toast_error = false;
    std::uint64_t toast_until = 0;
};

// Where things go on the 1920x1080 screen (the panel's layout at TV size).
namespace layout
{
constexpr int left = 72, right = 1848;
constexpr int header_cy = 70;
constexpr int tiles_y = 120, tiles_h = 136;
constexpr int content_top = tiles_y + tiles_h + 28, content_bottom = 972;
constexpr int side_w = 520; // the right-hand card next to a main one
constexpr int gap = 28;
} // namespace layout

App &state() noexcept;
void draw_screen(screen::Canvas &c) noexcept;
void toast(const char *message, bool error = false) noexcept;
// Asks breeze.elf for its state now (after a change), not at the next second.
void refresh_now() noexcept;
[[nodiscard]] bool same_curve(const link::Curve &a, const link::Curve &b) noexcept;
// Arms a confirmation, or returns true when it was armed and is still valid.
[[nodiscard]] bool confirmed(int what) noexcept;
} // namespace breeze::app

namespace breeze::views
{
using screen::Canvas;
// Each returns true when the screen must be drawn again.
bool curve_input(unsigned pressed) noexcept;
void curve_draw(Canvas &c) noexcept;
bool history_input(unsigned pressed) noexcept;
void history_draw(Canvas &c) noexcept;
[[nodiscard]] int history_seconds() noexcept;
bool presets_input(unsigned pressed) noexcept;
void presets_draw(Canvas &c) noexcept;
bool settings_input(unsigned pressed) noexcept;
// The touchpad, every frame on the Curve and History tabs.
bool curve_touch(const pad::Touch &t) noexcept;
bool history_touch(const pad::Touch &t) noexcept;
void settings_draw(Canvas &c) noexcept;
// The running curve drawn small, for preset rows.
void mini_curve(Canvas &c, int x, int y, int w, int h, const link::Curve &curve,
                screen::Color color) noexcept;
} // namespace breeze::views
