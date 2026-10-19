/*
 * Breeze app - renders each screen on a PC (Linux/WSL) with sample data, to
 * compare with the web panel without a console: app/preview/run.sh.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app.hpp"
#include "pad.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace breeze::pad
{
bool init() noexcept
{
    return true;
}
unsigned poll() noexcept
{
    return 0;
}
Touch touch() noexcept
{
    return Touch{false, 0, 0};
}
} // namespace breeze::pad

namespace
{
using namespace breeze;

void set_curve(link::Curve &c, const int (*pts)[2], int n) noexcept
{
    c.count = n;
    for (int i = 0; i < n; ++i)
    {
        c.temp[i] = pts[i][0];
        c.fan[i] = pts[i][1];
    }
}

void shot(const char *dir, const char *name) noexcept
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    screen::preview(app::draw_screen, path);
    printf("wrote %s\n", path);
}
} // namespace

int main(int argc, char **argv)
{
    const char *fonts = argc > 1 ? argv[1] : "app/fonts/";
    const char *out = argc > 2 ? argv[2] : ".";
    if (!font::load(fonts))
    {
        fprintf(stderr, "fonts missing in %s\n", fonts);
        return 1;
    }
    app::App &a = app::state();
    a.phase = app::Phase::running;
    a.port = 2878;
    strcpy(a.ip, "192.168.0.191");
    link::State &s = a.state;
    strcpy(s.version, "1.7");
    strcpy(s.preset, "Mine");
    s.enabled = 1;
    s.cpu = 72;
    s.soc = 71;
    s.duty = 33;
    s.desired = 32;
    s.target_c100 = 7206;
    s.stock_c100 = 9100;
    s.console_min = 23;
    s.at_floor = 0;
    static const int mine[][2] = {{50, 13}, {55, 25}, {60, 27}, {70, 30}, {74, 35}, {78, 39}, {80, 44}};
    set_curve(s.curve, mine, 7);
    a.draft = s.curve;
    a.selected = 2;

    static const int def[][2] = {{50, 13}, {55, 23}, {60, 23}, {70, 26}, {75, 32}, {78, 37}, {80, 40}};
    static const int quiet[][2] = {{50, 13}, {60, 15}, {68, 20}, {75, 28}, {82, 45}};
    a.presets.count = 3;
    strcpy(a.presets.list[0].name, "Breeze");
    set_curve(a.presets.list[0].curve, def, 7);
    strcpy(a.presets.list[1].name, "Mine");
    set_curve(a.presets.list[1].curve, mine, 7);
    strcpy(a.presets.list[2].name, "Quiet");
    set_curve(a.presets.list[2].curve, quiet, 5);
    a.presets_valid = true;
    a.preset_selected = 1;

    link::History &h = a.history;
    h.step_s = 2;
    h.count = 150;
    for (int i = 0; i < h.count; ++i)
    {
        const bool gap = i >= 40 && i < 46;
        const double t = 72 + 2.5 * sin(i / 9.0) + ((i * 37) % 7 - 3) * 0.3;
        h.temp[i] = static_cast<signed char>(gap ? -1 : static_cast<int>(t));
        h.want[i] = static_cast<signed char>(gap ? -1 : 26 + static_cast<int>((t - 70) * 1.2));
        h.fan[i] = static_cast<signed char>(gap ? -1 : h.want[i] + ((i * 11) % 5) - 2);
    }
    a.history_valid = true;

    a.tab = app::Tab::curve;
    shot(out, "1_curve");
    s.enabled = 0;
    shot(out, "1d_curve_off");
    s.enabled = 1;
    a.draft.fan[2] = 31;
    a.dirty = true;
    shot(out, "2_curve_edit");
    a.draft = s.curve;
    a.dirty = false;
    // The touchpad moving the selected point 3.
    a.drag = 2;
    a.selected = 2;
    a.draft.temp[2] = 62;
    a.draft.fan[2] = 34;
    a.dirty = true;
    shot(out, "1c_curve_drag");
    a.drag = -1;
    a.draft = s.curve;
    a.dirty = false;
    a.tab = app::Tab::history;
    a.touching = true;
    a.touch_x = 0.62f;
    shot(out, "3b_history_touch");
    a.touching = false;
    shot(out, "3_history");
    // 24 hours: the axis and a look back past an hour.
    a.range = 6;
    h.step_s = 288;
    a.touching = true;
    a.touch_x = 0.95f;
    shot(out, "3c_history_24h");
    a.range = 4;
    h.step_s = 36;
    a.touch_x = 0.62f;
    shot(out, "3d_history_3h");
    a.touching = false;
    a.range = 0;
    h.step_s = 2;
    a.tab = app::Tab::presets;
    // Two identical presets: only Breeze's active one is marked.
    a.presets.count = 4;
    strcpy(a.presets.list[3].name, "Mine again");
    set_curve(a.presets.list[3].curve, mine, 7);
    strcpy(s.preset, "Mine again");
    shot(out, "4_presets");
    strcpy(s.preset, "Mine");
    a.tab = app::Tab::settings;
    a.setting_selected = 1;
    app::toast("Custom curve on");
    s.update_check = 1;
    shot(out, "5_settings");
    a.toast_until = 0;
    // A newer Breeze is out: the badge in the header, the line in Settings.
    strcpy(s.latest, "1.9");
    a.setting_selected = 2;
    shot(out, "5b_settings_update");
    s.latest[0] = '\0';
    a.phase = app::Phase::starting;
    shot(out, "6_starting");
    // An older Breeze answered: the app's own copy is replacing it.
    strcpy(s.version, "1.8");
    a.phase = app::Phase::updating;
    shot(out, "7_updating");
    return 0;
}
