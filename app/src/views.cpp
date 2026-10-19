/*
 * Breeze app - the four screens: Curve, History, Presets, Settings, drawn in
 * the web panel's style.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app.hpp"
#include "github_mark.hpp"
#include "pad.hpp"
#include "ui.hpp"

extern "C"
{
#include "curve.h" // breeze.elf's own curve maths
}

#include <cmath>
#include <stdio.h>
#include <string.h>

namespace breeze::views
{
namespace
{
using app::App;
using font::Face;
using screen::Color;
using ui::px;
namespace L = app::layout;

constexpr int temp_min = FC_TEMP_MIN_C, temp_max = FC_TEMP_MAX_C;
constexpr int fan_min = FC_FAN_MIN_PCT, fan_max = 100;
constexpr int app_min = 23; // the console's minimum while a game or app runs

struct Plot
{
    float x0, y0, x1, y1;
    double t0, t1, v0, v1;

    [[nodiscard]] float x(double t) const noexcept
    {
        return static_cast<float>(x0 + (t - t0) * (x1 - x0) / (t1 - t0));
    }
    [[nodiscard]] float y(double v) const noexcept
    {
        return static_cast<float>(y1 - (v - v0) * (y1 - y0) / (v1 - v0));
    }
};

int to_points(const link::Curve &c, curve_point *p) noexcept
{
    for (int i = 0; i < c.count; ++i)
    {
        p[i].temp_c = c.temp[i];
        p[i].fan_pct = c.fan[i];
    }
    return c.count;
}

// What breeze.elf steers to at a whole degree.
int curve_value(const link::Curve &c, int t) noexcept
{
    curve_point p[link::max_points];
    const int n = to_points(c, p);
    return n > 0 ? fc_eval_points(p, n, t) : fan_min;
}

// The same curve between whole degrees, for drawing: the exact line that
// breeze.elf's integer values are truncated from.
double curve_at(const link::Curve &c, double t) noexcept
{
    curve_point p[link::max_points];
    const int n = to_points(c, p);
    if (n < 1)
        return fan_min;
    if (t <= p[0].temp_c)
        return p[0].fan_pct;
    for (int k = 0; k + 1 < n; ++k)
    {
        if (t > p[k + 1].temp_c)
            continue;
        const int h = p[k + 1].temp_c - p[k].temp_c;
        const int d = p[k + 1].fan_pct - p[k].fan_pct;
        if (h <= 0)
            return p[k + 1].fan_pct;
        return p[k].fan_pct + (t - p[k].temp_c) * d / h;
    }
    return p[n - 1].fan_pct;
}

// The curve sampled across a plot, as points for Canvas::polyline.
int curve_points(const Plot &p, const link::Curve &curve, float *xy, int cap) noexcept
{
    int n = 0;
    for (float x = p.x0; n < cap; x += 3)
    {
        const float cx = x < p.x1 ? x : p.x1;
        const double t = p.t0 + (cx - p.x0) * (p.t1 - p.t0) / (p.x1 - p.x0);
        xy[2 * n] = cx;
        xy[2 * n + 1] = p.y(curve_at(curve, t));
        ++n;
        if (cx >= p.x1)
            break;
    }
    return n;
}

void mono_label(Canvas &c, float x, float baseline, const char *s, int align, Color color = Color::faint) noexcept
{
    const int size = px(10.5f);
    const float w = Canvas::text_width(s, Face::mono_medium, size);
    const float at = align < 0 ? x : align > 0 ? x - w : x - w / 2;
    c.text(at, baseline, s, Face::mono_medium, size, color);
}

// A coloured badge with dark text, like the chart's live temp and fan labels.
void badge(Canvas &c, float cx, float cy, int w, const char *s, Color fill) noexcept
{
    const int h = px(19);
    c.round_rect(static_cast<int>(cx - w / 2.0f), static_cast<int>(cy - h / 2.0f), w, h, px(6), fill);
    const int size = px(10.5f);
    ui::text_center(c, cx, ui::baseline_for(static_cast<int>(cy), Face::mono_semibold, size), s,
                    Face::mono_semibold, size, Color::bg);
}

// .legend: coloured bars and names along a card's bottom.
void legend(Canvas &c, int x, int y, int w, const char *const *names, const Color *colors, int n) noexcept
{
    c.rect(x + ui::border, y, w - 2 * ui::border, ui::border, Color::line);
    int lx = x + px(16);
    const int cy = y + px(21);
    for (int i = 0; i < n; ++i)
    {
        c.round_rect(lx, cy - 2, px(14), 4, 2, colors[i]);
        lx += px(14) + px(7);
        lx += static_cast<int>(c.text(lx, ui::baseline_for(cy, Face::medium, px(12)), names[i], Face::medium,
                                      px(12), Color::muted)) +
              px(16);
    }
}

// ---- Curve -----------------------------------------------------------------

constexpr int main_w = L::right - L::left - L::side_w - L::gap;
constexpr int legend_h = px(42);

// The chart inside the Curve card (below its header, above its legend).
Plot curve_plot() noexcept
{
    const int top = L::content_top, h = L::content_bottom - L::content_top, hd = px(52);
    return Plot{static_cast<float>(L::left + px(44)), static_cast<float>(top + hd + px(18)),
                static_cast<float>(L::left + main_w - px(18)), static_cast<float>(top + h - legend_h - px(34)),
                temp_min, temp_max, fan_min, fan_max};
}

// A small readout box (the panel's .readout) with its top left at x, y.
void readout_box(Canvas &c, float x, float y, const char *s) noexcept
{
    const int size = px(12), h = px(26);
    const int w = static_cast<int>(Canvas::text_width(s, Face::mono_medium, size)) + 2 * px(9);
    const int bx = static_cast<int>(x), by = static_cast<int>(y);
    c.round_rect(bx, by, w, h, px(8), Color::bg, 240);
    c.round_frame(bx, by, w, h, px(8), ui::border, Color::line2);
    c.text(bx + px(9), ui::baseline_for(by + h / 2, Face::mono_medium, size), s, Face::mono_medium, size,
           Color::text);
}

// "Gaming", "Breeze (built-in)" or "Not in a preset": the active preset.
const char *preset_label(const link::State &s) noexcept
{
    static char out[64];
    if (!s.preset[0])
        return "Not in a preset";
    (void)snprintf(out, sizeof out, "%s%s", s.preset, strcmp(s.preset, link::builtin_preset) ? "" : " (built-in)");
    return out;
}

void select_clamp(App &a) noexcept
{
    if (a.selected >= a.draft.count)
        a.selected = a.draft.count - 1;
    if (a.selected < 0)
        a.selected = 0;
}

void add_point(App &a) noexcept
{
    link::Curve &d = a.draft;
    const int s = a.selected;
    if (d.count >= link::max_points)
    {
        app::toast("30 points is the most a curve can have", true);
        return;
    }
    int at = -1, t = 0, fan = 0;
    if (s + 1 < d.count && d.temp[s + 1] - d.temp[s] >= 2)
    {
        at = s + 1;
        t = (d.temp[s] + d.temp[s + 1]) / 2;
        fan = curve_value(d, t);
    }
    else if (s + 1 == d.count && d.temp[s] < temp_max)
    {
        at = s + 1;
        t = d.temp[s] + 5 < temp_max ? d.temp[s] + 5 : temp_max;
        fan = d.fan[s];
    }
    else if (s > 0 && d.temp[s] - d.temp[s - 1] >= 2)
    {
        at = s;
        t = (d.temp[s - 1] + d.temp[s]) / 2;
        fan = curve_value(d, t);
    }
    if (at < 0)
    {
        app::toast("No room for a point here: move one first", true);
        return;
    }
    for (int i = d.count; i > at; --i)
    {
        d.temp[i] = d.temp[i - 1];
        d.fan[i] = d.fan[i - 1];
    }
    d.temp[at] = t;
    d.fan[at] = fan;
    ++d.count;
    a.selected = at;
}

void remove_point(App &a) noexcept
{
    link::Curve &d = a.draft;
    if (d.count <= 2)
    {
        app::toast("A curve needs at least 2 points", true);
        return;
    }
    for (int i = a.selected; i + 1 < d.count; ++i)
    {
        d.temp[i] = d.temp[i + 1];
        d.fan[i] = d.fan[i + 1];
    }
    --d.count;
    select_clamp(a);
}

// Adds c as the newest entry, dropping the oldest when full.
void push(link::Curve *list, int &count, const link::Curve &c) noexcept
{
    if (count == App::undo_max)
    {
        memmove(list, list + 1, sizeof list[0] * (App::undo_max - 1));
        --count;
    }
    list[count++] = c;
}

// The draft changed from before: one undo step, unless it goes on a run of
// nudges to the same point's fan or temperature.
void record(App &a, const link::Curve &before, app::Edit kind) noexcept
{
    const bool run = (kind == app::Edit::fan || kind == app::Edit::temp) && kind == a.last_edit &&
                     a.selected == a.last_point;
    if (!run)
        push(a.undo, a.undo_count, before);
    a.redo_count = 0;
    a.last_edit = kind;
    a.last_point = a.selected;
}

// L3: back one change; R3: forward one undone change.
void undo_redo(App &a, bool undo) noexcept
{
    link::Curve *from = undo ? a.undo : a.redo, *to = undo ? a.redo : a.undo;
    int &from_count = undo ? a.undo_count : a.redo_count;
    int &to_count = undo ? a.redo_count : a.undo_count;
    if (from_count == 0)
    {
        app::toast(undo ? "Nothing to undo" : "Nothing to redo");
        return;
    }
    push(to, to_count, a.draft);
    a.draft = from[--from_count];
    a.dirty = !app::same_curve(a.draft, a.state.curve);
    a.last_edit = app::Edit::none;
    select_clamp(a);
}

// A config POST's status as a toast: done on success (nothing when null).
bool say_status(App &a, int status, const char *done) noexcept
{
    if (status == 200)
    {
        if (done)
            app::toast(done);
        return true;
    }
    if (status == 500)
    {
        app::toast("Applied, but saving it to /data failed", true);
        return true;
    }
    if (status == 403)
    {
        a.have_token = false;
        app::toast("Breeze refused the change: try again", true);
    }
    else
        app::toast("Could not reach Breeze", true);
    return false;
}

// Sends a curve (or only the switch) and says how it went: done on success,
// nothing when done is null (the caller words it from out). A curve goes as an
// edit of preset (the active one), which Breeze saves it into.
bool send_config(App &a, int enabled, const link::Curve *curve, const char *done, const char *preset = nullptr,
                 link::State *out = nullptr) noexcept
{
    if (!a.have_token)
        a.have_token = link::load_token(a.port);
    const int status = link::post_config(a.port, enabled, curve, preset, out);
    app::refresh_now();
    if (curve)
        a.presets_valid = false; // the edit went into a preset
    return say_status(a, status, done);
}

// One setting (key=value) and how it went, as send_config.
bool send_setting(App &a, const char *key, int value, const char *done) noexcept
{
    if (!a.have_token)
        a.have_token = link::load_token(a.port);
    const int status = link::post_setting(a.port, key, value);
    app::refresh_now();
    return say_status(a, status, done);
}

// .step: a value in a bordered box with its controller keys beside it.
void stepper(Canvas &c, int x, int y, int w, const char *label, const char *value, const char *unit,
             ui::Key k) noexcept
{
    c.text(x, y + px(14), label, Face::semibold, px(13), Color::text);
    const int by = y + px(26), bh = px(36);
    c.round_rect(x, by, w, bh, ui::radius_sm, Color::surface2);
    c.round_frame(x, by, w, bh, ui::radius_sm, ui::border, Color::line2);
    const float base = ui::baseline_for(by + bh / 2, Face::mono_medium, px(15));
    const float vw = c.text(x + px(14), base, value, Face::mono_medium, px(15), Color::text);
    c.text(x + px(14) + vw + px(4), base, unit, Face::medium, px(12), Color::faint);
    const int kw = ui::key(c, 0, -1000, k); // measure
    ui::key(c, x + w - px(12) - kw, by + bh / 2, k);
}

void apply_bar(Canvas &c, bool curve_on) noexcept
{
    // In the key hints' row: it carries Apply and Revert itself.
    const int h = px(42);
    const int x = L::left, w = L::right - L::left, y = screen::height - px(9) - h;
    c.round_rect(x, y, w, h, ui::radius, Color::surface2, 235);
    c.round_frame(x, y, w, h, ui::radius, ui::border, Color::line2);
    const int cy = y + h / 2;
    const float base = ui::baseline_for(cy, Face::semibold, px(13));
    const float tw = c.text(x + px(16), base, "Unsaved changes", Face::semibold, px(13), Color::text);
    // Where Apply puts them: the active preset, or a new one for the built-in one.
    const link::State &s = app::state().state;
    const bool to_preset = s.preset[0] && strcmp(s.preset, link::builtin_preset) != 0;
    char note[96];
    (void)snprintf(note, sizeof note, "Apply saves %s%s%s", to_preset ? "them to " : "them as a new preset",
                   to_preset ? s.preset : "", curve_on ? "" : " and turns the curve on");
    c.text(x + px(16) + tw + px(8), base, note, Face::regular, px(13), Color::muted);
    // [L3] Undo  [R3] Redo  [○] Revert  [✕] Apply, the last one primary.
    const int bh = px(34);
    const int apply_w = ui::chip(c, -10000, 0, bh, "Apply", true);
    const int revert_w = ui::chip(c, -10000, 0, bh, "Revert", false);
    const int kw = ui::key(c, -10000, 0, ui::Key::cross);
    int bx = x + w - px(12) - apply_w;
    ui::chip(c, bx, cy - bh / 2, bh, "Apply", true);
    bx -= px(8) + kw;
    ui::key(c, bx, cy, ui::Key::cross);
    bx -= px(16) + revert_w;
    ui::chip(c, bx, cy - bh / 2, bh, "Revert", false);
    bx -= px(8) + kw;
    ui::key(c, bx, cy, ui::Key::circle);
    bx -= px(24) + ui::key_hint(c, -10000, cy, ui::Key::r3, "Redo");
    ui::key_hint(c, bx, cy, ui::Key::r3, "Redo");
    bx -= px(18) + ui::key_hint(c, -10000, cy, ui::Key::l3, "Undo");
    ui::key_hint(c, bx, cy, ui::Key::l3, "Undo");
}
} // namespace

bool curve_input(unsigned pressed) noexcept
{
    App &a = app::state();
    link::Curve &d = a.draft;
    if (d.count < 1)
        return false;
    int &s = a.selected;
    if (pressed & (pad::l3 | pad::r3))
    {
        undo_redo(a, (pressed & pad::l3) != 0);
        return true;
    }
    const link::Curve before = d;

    if ((pressed & pad::left) && s > 0)
        --s;
    if ((pressed & pad::right) && s + 1 < d.count)
        ++s;
    if (pressed & (pad::up | pad::down))
    {
        const int f = d.fan[s] + ((pressed & pad::up) ? 1 : -1);
        d.fan[s] = f < fan_min ? fan_min : f > fan_max ? fan_max : f;
    }
    if (pressed & (pad::l2 | pad::r2))
    {
        const int lo = s > 0 ? d.temp[s - 1] + 1 : temp_min;
        const int hi = s + 1 < d.count ? d.temp[s + 1] - 1 : temp_max;
        const int t = d.temp[s] + ((pressed & pad::r2) ? 1 : -1);
        d.temp[s] = t < lo ? lo : t > hi ? hi : t;
    }
    if (pressed & pad::triangle)
        add_point(a);
    if (pressed & pad::square)
        remove_point(a);
    a.dirty = !app::same_curve(d, a.state.curve);

    if ((pressed & pad::cross) && !a.dirty)
    {
        // Nothing to save: with the curve off, Apply turns it on.
        if (a.state.enabled != 1)
            (void)send_config(a, 1, nullptr, "Custom curve on");
    }
    else if (pressed & pad::cross)
    {
        // An edit of the active preset: Breeze saves it there, or as a new one.
        const bool was_on = a.state.enabled == 1;
        char from[sizeof a.state.preset];
        (void)snprintf(from, sizeof from, "%s", a.state.preset);
        link::State after{};
        if (send_config(a, 1, &d, nullptr, from, &after))
        {
            a.dirty = false;
            if (after.version[0]) // 200; send_config said what went wrong otherwise
            {
                char msg[96];
                const char *on = was_on ? "" : "Custom curve on · ";
                if (!after.preset[0])
                    (void)snprintf(msg, sizeof msg, "%sApplied, but the preset list is full (10)", on);
                else
                    (void)snprintf(msg, sizeof msg, "%sSaved %s %s", on, strcmp(after.preset, from) ? "as" : "to",
                                   after.preset);
                app::toast(msg, !after.preset[0]);
            }
        }
    }
    if ((pressed & pad::circle) && a.dirty)
    {
        d = a.state.curve;
        a.dirty = false;
        select_clamp(a);
    }

    // Nudges alone go on a run; anything else ends it, even when it changed
    // nothing (Revert is a step too).
    const app::Edit kind = !(pressed & ~(pad::up | pad::down))   ? app::Edit::fan
                           : !(pressed & ~(pad::l2 | pad::r2)) ? app::Edit::temp
                                                               : app::Edit::other;
    if (!app::same_curve(before, d))
        record(a, before, kind);
    else if (kind != a.last_edit)
        a.last_edit = app::Edit::none;
    return true;
}

void curve_draw(Canvas &c) noexcept
{
    const App &a = app::state();
    const link::Curve &d = a.draft;
    const link::State &s = a.state;
    const int top = L::content_top, h = L::content_bottom - L::content_top;

    // ---- the chart card
    ui::card(c, L::left, top, main_w, h);
    char sub[128];
    (void)snprintf(sub, sizeof sub, "%s · %d / %d points%s", preset_label(s), d.count, link::max_points,
                   s.enabled != 1 && !a.dirty ? " · the custom curve is off: Apply turns it on" : "");
    ui::card_header(c, L::left, top, main_w, "Curve", sub);
    const Plot p = curve_plot();

    const float fan_y = s.duty >= 0 ? p.y(s.duty < fan_min ? fan_min : s.duty > 100 ? 100 : s.duty) : -1000;
    const float cpu_x = s.cpu >= 0 ? p.x(s.cpu < temp_min ? temp_min : s.cpu > temp_max ? temp_max : s.cpu) : -1000;
    for (int v = fan_min; v <= 100; v = v < 20 ? 20 : v + 10)
    {
        const float y = p.y(v);
        c.rect(static_cast<int>(p.x0), static_cast<int>(y), static_cast<int>(p.x1 - p.x0), 2,
               v % 20 ? Color::grid_minor : Color::grid_major);
        const bool near_fan = std::fabs(y - fan_y) < px(16);
        if ((v % 20 == 0 || v == fan_min) && !near_fan)
        {
            char label[8];
            (void)snprintf(label, sizeof label, "%d%%", v);
            mono_label(c, p.x0 - px(9), y + px(3.5f), label, 1);
        }
    }
    for (int t = temp_min; t <= temp_max; t += 5)
    {
        const float x = p.x(t);
        c.rect(static_cast<int>(x), static_cast<int>(p.y0), 2, static_cast<int>(p.y1 - p.y0),
               t % 10 ? Color::grid_minor : Color::grid_major);
        if (t % 10 == 0 && std::fabs(x - cpu_x) >= px(34))
        {
            char label[8];
            (void)snprintf(label, sizeof label, "%d°", t);
            mono_label(c, x, p.y1 + px(19), label, 0);
        }
    }
    // The console's minimum while an app runs: in a game the curve can't go below this band.
    {
        const float y = p.y(app_min);
        c.rect(static_cast<int>(p.x0), static_cast<int>(y), static_cast<int>(p.x1 - p.x0),
               static_cast<int>(p.y(fan_min) - y), Color::white, 9);
        c.dashed_h(static_cast<int>(p.x0), static_cast<int>(p.x1), static_cast<int>(y), 2, px(3), px(4),
                   Color::faint, 179);
        mono_label(c, p.x(temp_min) + px(8), y + px(14), "app minimum", -1);
    }

    static float xy[2 * 1024];
    if (d.count > 0)
    {
        const int n = curve_points(p, d, xy, 1024);
        c.fill_under(xy, n, static_cast<int>(p.y(fan_min)), Color::accent, 56);
        if (a.dirty && s.curve.count > 0)
        {
            static float old[2 * 1024];
            const int m = curve_points(p, s.curve, old, 1024);
            c.polyline(old, m, px(1.5f), Color::muted, 115, px(4), px(4));
        }
        c.polyline(xy, n, px(2.4f), Color::accent);
    }

    // "Now": the temp (badge on the x axis) and the real fan speed (badge on the y axis).
    if (s.cpu >= 0)
    {
        c.dashed_v(static_cast<int>(cpu_x), static_cast<int>(p.y0), static_cast<int>(p.y1), 2, px(2), px(4),
                   Color::temp, 89);
        if (s.duty >= 0)
        {
            c.dashed_h(static_cast<int>(p.x0), static_cast<int>(p.x1), static_cast<int>(fan_y), 2, px(2), px(4),
                       Color::fan, 89);
            char v[8];
            (void)snprintf(v, sizeof v, "%d%%", s.duty);
            badge(c, L::left + px(23), fan_y, px(40), v, Color::fan);
        }
        char v[8];
        (void)snprintf(v, sizeof v, "%d°", s.cpu);
        badge(c, cpu_x, p.y1 + px(14.5f), px(42), v, Color::temp);
    }

    const float R = px(6);
    for (int i = 0; i < d.count; ++i)
    {
        const float x = p.x(d.temp[i]), y = p.y(d.fan[i]);
        const bool on = i == a.selected || i == a.drag;
        if (on)
            c.circle(x, y, R + px(7), Color::accent, i == a.drag ? 90 : 46);
        c.circle(x, y, on ? R + px(1) : R, on ? Color::white : Color::bg);
        c.ring(x, y, on ? R + px(1) : R, px(2.2f), Color::accent);
    }
    if (a.drag >= 0 && a.drag < d.count)
    {
        char v[32];
        (void)snprintf(v, sizeof v, "%d°C · %d%%", d.temp[a.drag], d.fan[a.drag]);
        const float x = p.x(d.temp[a.drag]), y = p.y(d.fan[a.drag]);
        readout_box(c, x + px(16), y - px(40) < p.y0 ? y + px(14) : y - px(40), v);
    }

    static const char *const names[] = {"Your curve", "Current temp", "Actual fan"};
    static const Color colors[] = {Color::accent, Color::temp, Color::fan};
    legend(c, L::left, top + h - legend_h, main_w, names, colors, 3);

    // ---- the selected point
    const int sx = L::left + main_w + L::gap;
    ui::card(c, sx, top, L::side_w, h);
    char title[32];
    (void)snprintf(title, sizeof title, "of %d", d.count);
    char name[16];
    (void)snprintf(name, sizeof name, "Point %d", a.selected + 1);
    const int shd = ui::card_header(c, sx, top, L::side_w, name, title);
    if (d.count > 0)
    {
        char t[8], f[8];
        (void)snprintf(t, sizeof t, "%d", d.temp[a.selected]);
        (void)snprintf(f, sizeof f, "%d", d.fan[a.selected]);
        const int bx = sx + px(16), bw = L::side_w - 2 * px(16);
        stepper(c, bx, top + shd + px(14), bw, "Temperature", t, "°C", ui::Key::l2_r2);
        stepper(c, bx, top + shd + px(90), bw, "Fan", f, "%", ui::Key::up_down);
        int y = top + shd + px(172);
        c.rect(sx + ui::border, y, L::side_w - 2 * ui::border, ui::border, Color::line);
        y += px(24);
        ui::key_hint(c, bx, y, ui::Key::left_right, "Choose a point");
        ui::key_hint(c, bx, y + px(30), ui::Key::triangle, "Add a point after it");
        ui::key_hint(c, bx, y + px(60), ui::Key::square, "Remove it");
        ui::key_hint(c, bx, y + px(90), ui::Key::touchpad, "Slide to move it");
        const int uw = ui::key_hint(c, bx, y + px(120), ui::Key::l3, "Undo");
        ui::key_hint(c, bx + uw + px(18), y + px(120), ui::Key::r3, "Redo");
    }

    if (a.dirty)
    {
        apply_bar(c, s.enabled == 1);
        return;
    }
    static const ui::Hint hints[] = {
        {ui::Key::left_right, "Point"}, {ui::Key::up_down, "Fan"},   {ui::Key::l2_r2, "Temperature"},
        {ui::Key::triangle, "Add"},     {ui::Key::square, "Remove"}, {ui::Key::touchpad, "Move"},
        {ui::Key::l3, "Undo"},          {ui::Key::r3, "Redo"},       {ui::Key::cross, "Apply"},
        {ui::Key::circle, "Revert"},
    };
    ui::hint_bar(c, hints, 10);
}

namespace
{
int history_hover(const App &a) noexcept;
} // namespace

// The touchpad on the Curve tab: sliding a finger moves the selected point,
// the way a laptop's touchpad moves the pointer. The pad's width spans the
// chart's temperatures and its height the fan range.
bool curve_touch(const pad::Touch &t) noexcept
{
    App &a = app::state();
    link::Curve &d = a.draft;
    // Where the finger was when this slide began, the draft then (what Undo
    // goes back to), and the draft as the slide last left it.
    static float from_x, from_y;
    static link::Curve start, seen;
    static bool recorded;
    if (!t.down || d.count < 1)
    {
        const bool changed = a.drag >= 0;
        a.drag = -1;
        return changed;
    }
    const int i = a.selected;
    // A new slide, another point chosen, or the buttons (or Undo) changed the
    // curve meanwhile: go on from here.
    if (a.drag != i || !app::same_curve(d, seen))
    {
        a.drag = i;
        from_x = t.x;
        from_y = t.y;
        start = seen = d;
        recorded = false;
        return true;
    }
    const Plot p = curve_plot();
    const int lo = i > 0 ? d.temp[i - 1] + 1 : temp_min;
    const int hi = i + 1 < d.count ? d.temp[i + 1] - 1 : temp_max;
    int nt = start.temp[i] + static_cast<int>(std::lround((t.x - from_x) * (p.t1 - p.t0)));
    int nf = start.fan[i] - static_cast<int>(std::lround((t.y - from_y) * (p.v1 - p.v0)));
    nt = nt < lo ? lo : nt > hi ? hi : nt;
    nf = nf < fan_min ? fan_min : nf > fan_max ? fan_max : nf;
    if (nt == d.temp[i] && nf == d.fan[i])
        return false;
    // The whole slide is one undo step.
    if (!recorded)
    {
        record(a, start, app::Edit::other);
        recorded = true;
    }
    d.temp[i] = nt;
    d.fan[i] = nf;
    seen = d;
    a.dirty = !app::same_curve(d, a.state.curve);
    return true;
}

bool history_touch(const pad::Touch &t) noexcept
{
    App &a = app::state();
    if (!t.down)
    {
        const bool changed = a.touching;
        a.touching = false;
        return changed;
    }
    const int before = history_hover(a);
    const bool was = a.touching;
    a.touching = true;
    a.touch_x = t.x;
    a.touch_y = t.y;
    return !was || history_hover(a) != before;
}

// ---- History ---------------------------------------------------------------

namespace
{
struct Range
{
    int seconds, tick;
    const char *label;
};
constexpr Range ranges[] = {{300, 60, "5m"},       {900, 300, "15m"},      {1800, 600, "30m"},
                            {3600, 900, "1h"},      {10800, 1800, "3h"},    {43200, 7200, "12h"},
                            {86400, 14400, "24h"}};
constexpr int range_count = sizeof ranges / sizeof ranges[0];

// The chart inside the History card.
Plot history_plot(int seconds) noexcept
{
    const int top = L::content_top, h = L::content_bottom - L::content_top, w = L::right - L::left, hd = px(52);
    return Plot{static_cast<float>(L::left + px(36) + px(16)), static_cast<float>(top + hd + px(12) + px(8)),
                static_cast<float>(L::left + w - px(14) - px(16)),
                static_cast<float>(top + h - legend_h - px(22) - px(8)), 0, static_cast<double>(seconds), 0, 100};
}

// The sample under the touchpad's finger, -1 when none.
int history_hover(const App &a) noexcept
{
    const link::History &hs = a.history;
    if (!a.touching || !a.history_valid || hs.count < 1)
        return -1;
    const int step = hs.step_s > 0 ? hs.step_s : 2;
    const int seconds = ranges[a.range].seconds;
    const double age = seconds * (1.0 - a.touch_x);
    int i = hs.count - 1 - static_cast<int>(age / step + 0.5);
    return i < 0 ? 0 : i >= hs.count ? hs.count - 1 : i;
}

// An axis label: −30m, −1h, −1.5h.
void ago(char *out, size_t cap, int sec) noexcept
{
    if (sec < 3600)
        (void)snprintf(out, cap, "−%dm", sec / 60);
    else if (sec % 3600 == 0)
        (void)snprintf(out, cap, "−%dh", sec / 3600);
    else
        (void)snprintf(out, cap, "−%d.%dh", sec / 3600, sec % 3600 * 10 / 3600);
}

// One series; a missing value (-1) breaks the line, a lone value is a dot.
void series(Canvas &c, const Plot &p, const signed char *v, int n, int step, int range, float width,
            Color color, float dash) noexcept
{
    static float xy[2 * link::max_history];
    int run = 0;
    for (int i = 0; i <= n; ++i)
    {
        const int val = i < n ? v[i] : -1;
        if (val >= 0)
        {
            const double age = static_cast<double>(n - 1 - i) * step;
            xy[2 * run] = p.x(range - age);
            xy[2 * run + 1] = p.y(val > 100 ? 100 : val);
            ++run;
            continue;
        }
        if (run == 1)
            c.circle(xy[0], xy[1], width, color);
        else if (run > 1)
            c.polyline(xy, run, width, color, 255, dash, dash);
        run = 0;
    }
}
} // namespace

int history_seconds() noexcept
{
    return ranges[app::state().range].seconds;
}

bool history_input(unsigned pressed) noexcept
{
    App &a = app::state();
    const int before = a.range;
    if ((pressed & (pad::left | pad::l2)) && a.range > 0)
        --a.range;
    if ((pressed & (pad::right | pad::r2)) && a.range + 1 < range_count)
        ++a.range;
    if (a.range == before)
        return false;
    a.history_valid = false;
    a.history_due = 0;
    return true;
}

void history_draw(Canvas &c) noexcept
{
    const App &a = app::state();
    const int top = L::content_top, h = L::content_bottom - L::content_top, w = L::right - L::left;
    ui::card(c, L::left, top, w, h);
    const int hd = ui::card_header(c, L::left, top, w, "History", "temperature, fan and what the curve wanted");

    // #ranges: a segmented control at the header's right.
    {
        const char *labels[range_count];
        for (int i = 0; i < range_count; ++i)
            labels[i] = ranges[i].label;
        const int cy = top + hd / 2, size = px(12), pad_x = px(10);
        const int total = ui::segmented(c, -1, cy, labels, range_count, 0, Face::mono_medium, size, pad_x);
        const int seg_x = L::left + w - px(16) - total;
        ui::segmented(c, seg_x, cy, labels, range_count, a.range, Face::mono_medium, size, pad_x);
        const int kw = ui::key(c, -10000, 0, ui::Key::left_right);
        ui::key(c, seg_x - px(10) - kw, cy, ui::Key::left_right);
    }

    const Range &r = ranges[a.range];
    const Plot p = history_plot(r.seconds);
    for (int v = 0; v <= 100; v += 25)
    {
        const float y = p.y(v);
        c.rect(static_cast<int>(p.x0), static_cast<int>(y), static_cast<int>(p.x1 - p.x0), 2, Color::grid_major);
        char label[8];
        (void)snprintf(label, sizeof label, "%d", v);
        mono_label(c, p.x0 - px(8), y + px(3.5f), label, 1);
    }
    for (int age = r.tick; age <= r.seconds; age += r.tick)
    {
        const float x = p.x(r.seconds - age);
        c.rect(static_cast<int>(x), static_cast<int>(p.y0), 2, static_cast<int>(p.y1 - p.y0), Color::grid_minor);
        char label[12];
        ago(label, sizeof label, age);
        mono_label(c, x, p.y1 + px(17), label, age == r.seconds ? -1 : 0);
    }
    mono_label(c, p.x1, p.y1 + px(17), "now", 1);

    const link::History &hs = a.history;
    if (!a.history_valid || hs.count < 2)
        ui::text_center(c, (p.x0 + p.x1) / 2, (p.y0 + p.y1) / 2, a.history_valid ? "No history yet" : "Loading…",
                        Face::medium, px(14), Color::faint);
    else
    {
        const int step = hs.step_s > 0 ? hs.step_s : 2;
        series(c, p, hs.want, hs.count, step, r.seconds, px(1.4f), Color::accent, px(3));
        series(c, p, hs.temp, hs.count, step, r.seconds, px(1.8f), Color::temp, 0);
        series(c, p, hs.fan, hs.count, step, r.seconds, px(1.8f), Color::fan, 0);

        // Touchpad: a crosshair and the values at that moment (the panel's hover).
        const int i = history_hover(a);
        if (i >= 0)
        {
            const int age = (hs.count - 1 - i) * step;
            const float x = p.x(r.seconds - age);
            c.rect(static_cast<int>(x), static_cast<int>(p.y0), 2, static_cast<int>(p.y1 - p.y0), Color::muted,
                   102);
            const signed char *vals[3] = {hs.temp, hs.fan, hs.want};
            static const Color cols[3] = {Color::temp, Color::fan, Color::accent};
            for (int k = 0; k < 3; ++k)
                if (vals[k][i] >= 0)
                {
                    const float y = p.y(vals[k][i] > 100 ? 100 : vals[k][i]);
                    c.circle(x, y, px(3.5f) + 2, Color::bg);
                    c.circle(x, y, px(3.5f), cols[k]);
                }
            // .htip: when, then the three values.
            const int bw = px(130), bh = px(86), size = px(12);
            const float bx = x > (p.x0 + p.x1) / 2 ? x - px(12) - bw : x + px(12);
            const int by = static_cast<int>(p.y0);
            c.round_rect(static_cast<int>(bx), by, bw, bh, px(8), Color::bg, 240);
            c.round_frame(static_cast<int>(bx), by, bw, bh, px(8), ui::border, Color::line2);
            char when[24];
            if (age == 0)
                (void)snprintf(when, sizeof when, "now");
            else if (age < 60)
                (void)snprintf(when, sizeof when, "%ds ago", age);
            else if (age < 3600)
                (void)snprintf(when, sizeof when, "%dm %02ds ago", age / 60, age % 60);
            else
            {
                // Whole minutes, rounded first: 1h 59m 50s reads 2h 00m.
                const int m = (age + 30) / 60;
                (void)snprintf(when, sizeof when, "%dh %02dm ago", m / 60, m % 60);
            }
            const float lx = bx + px(9), rx = bx + bw - px(9);
            c.text(lx, by + px(19), when, Face::mono_medium, size, Color::faint);
            static const char *const keys[3] = {"Temp", "Fan", "Wanted"};
            static const char *const units[3] = {"°C", "%", "%"};
            for (int k = 0; k < 3; ++k)
            {
                const float base = by + px(39) + k * px(19);
                c.text(lx, base, keys[k], Face::mono_medium, size, Color::muted);
                char v[12];
                if (vals[k][i] >= 0)
                    (void)snprintf(v, sizeof v, "%d%s", vals[k][i], units[k]);
                else
                    (void)snprintf(v, sizeof v, "–");
                ui::text_right(c, rx, base, v, Face::mono_medium, size, cols[k]);
            }
        }
    }
    static const char *const names[] = {"Temp", "Fan", "Wanted"};
    static const Color colors[] = {Color::temp, Color::fan, Color::accent};
    legend(c, L::left, top + h - legend_h, w, names, colors, 3);

    static const ui::Hint hints[] = {{ui::Key::left_right, "Range"}, {ui::Key::touchpad, "Look back"}};
    ui::hint_bar(c, hints, 2);
}

// ---- Presets ---------------------------------------------------------------

void mini_curve(Canvas &c, int x, int y, int w, int h, const link::Curve &curve, Color color) noexcept
{
    const Plot p{static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w),
                 static_cast<float>(y + h), temp_min, temp_max, fan_min, fan_max};
    static float xy[2 * 512];
    const int n = curve_points(p, curve, xy, 512);
    c.polyline(xy, n, px(1.5f), color);
}

namespace
{
constexpr int delete_confirm = 100; // + preset index

int post_preset_checked(App &a, const char *action, const char *name, const link::Curve *curve = nullptr,
                        bool use = false) noexcept
{
    if (!a.have_token)
        a.have_token = link::load_token(a.port);
    const int status = link::post_preset(a.port, action, name, curve, use);
    if (status == 403)
        a.have_token = false;
    a.presets_valid = false;
    app::refresh_now();
    return status;
}

// The preset marked Active: Breeze's active preset, the one Apply saves into;
// none while the curve is off (the panel's rule).
int active_index(const App &a) noexcept
{
    if (a.state.enabled != 1)
        return -1;
    for (int i = 0; i < a.presets.count; ++i)
        if (strcmp(a.presets.list[i].name, a.state.preset) == 0)
            return i;
    return -1;
}
} // namespace

bool presets_input(unsigned pressed) noexcept
{
    App &a = app::state();
    const int n = a.presets.count;
    if ((pressed & pad::up) && a.preset_selected > 0)
        --a.preset_selected;
    if ((pressed & pad::down) && a.preset_selected + 1 < n)
        ++a.preset_selected;

    char msg[96];
    if ((pressed & pad::cross) && n > 0)
    {
        const char *name = a.presets.list[a.preset_selected].name;
        const int status = post_preset_checked(a, "apply", name);
        if (status == 200)
        {
            a.dirty = false; // the editor follows the curve just chosen
            (void)snprintf(msg, sizeof msg, "Using %s", name);
            app::toast(msg);
        }
        else
            app::toast(status == 500 ? "Applied, but saving it to /data failed" : "Could not use that preset", true);
    }
    if (pressed & pad::triangle)
    {
        // Breeze's names for new presets: "My curve", "My curve 2", ...
        char name[24] = "";
        for (int k = 1; k <= link::max_presets + 1 && !name[0]; ++k)
        {
            char candidate[24];
            if (k == 1)
                (void)snprintf(candidate, sizeof candidate, "My curve");
            else
                (void)snprintf(candidate, sizeof candidate, "My curve %d", k);
            bool taken = false;
            for (int i = 0; i < n; ++i)
                taken = taken || strcmp(a.presets.list[i].name, candidate) == 0;
            if (!taken)
                (void)snprintf(name, sizeof name, "%s", candidate);
        }
        // The curve in the editor becomes a preset of its own, and runs.
        const int status = post_preset_checked(a, "save", name, &a.draft, true);
        if (status == 200)
        {
            (void)snprintf(msg, sizeof msg, "Saved as %s and using it", name);
            app::toast(msg);
            a.dirty = false;
            a.preset_selected = n; // the new one, at the end
        }
        else
            app::toast(status == 409 ? "The preset list is full (10)" : "Could not save the preset", true);
    }
    if ((pressed & pad::square) && n > 0)
    {
        const char *name = a.presets.list[a.preset_selected].name;
        if (strcmp(name, link::builtin_preset) == 0)
            app::toast("The built-in preset can't be deleted", true);
        else if (app::confirmed(delete_confirm + a.preset_selected))
        {
            (void)snprintf(msg, sizeof msg, "Deleted %s", name);
            const int status = post_preset_checked(a, "delete", name);
            app::toast(status == 200 ? msg : "Could not delete the preset", status != 200);
        }
        else
        {
            (void)snprintf(msg, sizeof msg, "Press Square again to delete %s", name);
            app::toast(msg, true);
        }
    }
    return true;
}

void presets_draw(Canvas &c) noexcept
{
    const App &a = app::state();
    const int top = L::content_top, h = L::content_bottom - L::content_top;
    ui::card(c, L::left, top, main_w, h);
    char sub[24];
    (void)snprintf(sub, sizeof sub, "%d / %d", a.presets.count, link::max_presets);
    const int hd = ui::card_header(c, L::left, top, main_w, "Presets", a.presets_valid ? sub : "");

    const int bx = L::left + px(16), bw = main_w - 2 * px(16);
    if (!a.presets_valid)
        c.text(bx, top + hd + px(34), "Loading…", Face::medium, px(13), Color::faint);
    else if (a.presets.count == 0)
        c.text(bx, top + hd + px(34), "No presets yet. Triangle saves the running curve as one.", Face::medium,
               px(12.5f), Color::faint);
    const int row_h = px(56);
    const int active_at = active_index(a);
    for (int i = 0; a.presets_valid && i < a.presets.count; ++i)
    {
        const link::Preset &pr = a.presets.list[i];
        const int y = top + hd + px(6) + i * row_h;
        const int cy = y + row_h / 2;
        if (i == a.preset_selected)
            ui::selection(c, bx - px(8), y + px(4), bw + px(16), row_h - px(8));
        else if (i + 1 < a.presets.count && i + 1 != a.preset_selected)
            c.rect(bx, y + row_h - 1, bw, ui::border, Color::line);
        c.text(bx + px(6), ui::baseline_for(cy - px(9), Face::semibold, px(13.5f)), pr.name, Face::semibold,
               px(13.5f), Color::text);
        char meta[40];
        (void)snprintf(meta, sizeof meta, "%s%d points", strcmp(pr.name, link::builtin_preset) ? "" : "Built-in · ",
                       pr.curve.count);
        c.text(bx + px(6), ui::baseline_for(cy + px(10), Face::medium, px(11.5f)), meta, Face::medium, px(11.5f),
               Color::faint);
        const bool active = i == active_at;
        const int badge_w = ui::active_badge(c, -10000, 0, px(30));
        mini_curve(c, bx + px(260), cy - px(18), bw - px(260) - badge_w - px(36), px(36), pr.curve,
                   active ? Color::fan : Color::accent);
        if (active)
            ui::active_badge(c, bx + bw - px(6) - badge_w, cy - px(15), px(30));
    }

    const int sx = L::left + main_w + L::gap;
    ui::card(c, sx, top, L::side_w, h);
    const int shd = ui::card_header(c, sx, top, L::side_w, "Selected preset", "");
    const int x = sx + px(16);
    int y = top + shd + px(28);
    ui::key_hint(c, x, y, ui::Key::cross, "Use this curve");
    y += px(40);
    ui::key_hint(c, x, y, ui::Key::triangle, "New preset from the curve");
    y += px(40);
    ui::key_hint(c, x, y, ui::Key::square, "Delete it");
    ui::paragraph(c, x, y + px(56), L::side_w - 2 * px(16), px(19),
                  "Apply on the Curve tab saves your changes into the preset in use. The built-in Breeze preset "
                  "never changes: changes to it are saved as a new preset. Delete asks for a second press. "
                  "Rename presets in the web panel.",
                  Face::regular, px(12.5f), Color::faint);

    static const ui::Hint hints[] = {
        {ui::Key::up_down, "Choose"}, {ui::Key::cross, "Use"}, {ui::Key::triangle, "New"},
        {ui::Key::square, "Delete"}};
    ui::hint_bar(c, hints, 4);
}

// ---- Settings --------------------------------------------------------------

namespace
{
constexpr int setting_count = 3;
constexpr int reset_confirm = 200;
} // namespace

bool settings_input(unsigned pressed) noexcept
{
    App &a = app::state();
    if ((pressed & pad::up) && a.setting_selected > 0)
        --a.setting_selected;
    if ((pressed & pad::down) && a.setting_selected + 1 < setting_count)
        ++a.setting_selected;
    if (!(pressed & pad::cross))
        return true;

    const link::State &s = a.state;
    switch (a.setting_selected)
    {
    case 0:
        (void)send_config(a, s.enabled != 1, nullptr,
                          s.enabled != 1 ? "Custom curve on" : "Stock fan: the console runs it");
        break;
    case 1:
        if (app::confirmed(reset_confirm))
        {
            // The built-in preset, which keeps the default curve.
            const int status = post_preset_checked(a, "apply", link::builtin_preset);
            if (status == 200)
                a.dirty = false;
            app::toast(status == 200 ? "Using the built-in Breeze curve" : "Could not reset the curve", status != 200);
        }
        else
            app::toast("Press Cross again to reset the curve", true);
        break;
    default:
        (void)send_setting(a, "update_check", s.update_check != 1,
                           s.update_check != 1 ? "Update check on" : "Update check off");
        break;
    }
    return true;
}

void settings_draw(Canvas &c) noexcept
{
    const App &a = app::state();
    const link::State &s = a.state;
    const int top = L::content_top, h = L::content_bottom - L::content_top;
    ui::card(c, L::left, top, main_w, h);
    const int hd = ui::card_header(c, L::left, top, main_w, "Settings", "");
    char update_line[128];
    if (s.latest[0])
        (void)snprintf(update_line, sizeof update_line,
                       "Breeze %s is out. Get it in ProsperoStore or from github.com/MiladRTX/Breeze.", s.latest);
    else
        (void)snprintf(update_line, sizeof update_line, "Breeze asks GitHub once a day for a newer version.");

    const struct
    {
        const char *title, *sub;
        int kind; // 0 switch, 1 button
        bool on;
    } rows[setting_count] = {
        {"Custom curve", "Breeze steers the fan to your curve. Off: the console runs the fan itself.", 0,
         s.enabled == 1},
        {"Reset the curve", "Switches to the built-in Breeze preset: 23% up to 60 °C, rising to 40% at 80 °C.", 1,
         false},
        {"Check for updates", update_line, 0, s.update_check == 1},
    };
    const int bx = L::left + px(16), bw = main_w - 2 * px(16), row_h = px(66);
    for (int i = 0; i < setting_count; ++i)
    {
        const int y = top + hd + px(6) + i * row_h, cy = y + row_h / 2;
        if (i == a.setting_selected)
            ui::selection(c, bx - px(8), y + px(4), bw + px(16), row_h - px(8));
        else if (i + 1 < setting_count && i + 1 != a.setting_selected)
            c.rect(bx, y + row_h - 1, bw, ui::border, Color::line);
        c.text(bx + px(6), ui::baseline_for(cy - px(10), Face::semibold, px(13.5f)), rows[i].title, Face::semibold,
               px(13.5f), Color::text);
        c.text(bx + px(6), ui::baseline_for(cy + px(10), Face::regular, px(12)), rows[i].sub, Face::regular, px(12),
               i == 2 && s.latest[0] ? Color::accent : Color::faint);
        if (rows[i].kind == 0)
            ui::toggle(c, bx + bw - px(6) - ui::sw_w, cy - ui::sw_h / 2, rows[i].on);
        else
        {
            const int cw = ui::chip(c, -10000, 0, px(32), "Reset", false);
            ui::chip(c, bx + bw - px(6) - cw, cy - px(16), px(32), "Reset", false);
        }
    }

    const int sx = L::left + main_w + L::gap;
    ui::card(c, sx, top, L::side_w, h);
    const int shd = ui::card_header(c, sx, top, L::side_w, "About", "");
    const int x = sx + px(16), w = L::side_w - 2 * px(16);
    int y = top + shd + px(30);
    char line[96];
    auto info = [&](const char *label, const char *value) {
        ui::eyebrow(c, x, y, label);
        c.text(x, y + px(24), value, Face::mono_medium, px(13), Color::text);
        y += px(62);
    };
    if (a.ip[0])
        (void)snprintf(line, sizeof line, "http://%s:%d", a.ip, a.port);
    else
        (void)snprintf(line, sizeof line, "port %d", a.port);
    info("Web panel", line);
    if (s.stock_c100 > 0)
    {
        (void)snprintf(line, sizeof line, "%d.%d °C", (s.stock_c100 + 5) / 100, (s.stock_c100 + 5) / 10 % 10);
        info("The console's own fan target", line);
    }
    if (s.console_min > 0)
    {
        (void)snprintf(line, sizeof line, "%d%%", s.console_min);
        info("Fan minimum right now", line);
    }
    ui::paragraph(c, x, y + px(6), w, px(19),
                  "While this app is open the PS5 treats it like a game, so the fan minimum is the in-game 23%.",
                  Face::regular, px(12.5f), Color::faint);

    // The web panel's footer credit, at the card's foot. A TV can't follow a
    // link, so the address is written out.
    const int foot = top + h, icon = art::github_mark_size, icon_cy = foot - px(17);
    c.rect(sx + ui::border, foot - px(60), L::side_w - 2 * ui::border, ui::border, Color::line);
    c.text(x, foot - px(38), "Made by Milad", Face::medium, px(12.5f), Color::muted);
    c.icon(x, icon_cy - icon / 2, icon, icon, art::github_mark, Color::text);
    c.text(x + icon + px(8), ui::baseline_for(icon_cy, Face::mono_medium, px(13)), "github.com/MiladRTX/Breeze",
           Face::mono_medium, px(13), Color::text);

    static const ui::Hint hints[] = {{ui::Key::up_down, "Choose"}, {ui::Key::cross, "Change"}};
    ui::hint_bar(c, hints, 2);
}
} // namespace breeze::views
