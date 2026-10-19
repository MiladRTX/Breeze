/*
 * Breeze app - the web panel's components, drawn at TV size.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui.hpp"

#include <ctype.h>
#include <string.h>

namespace breeze::ui
{
float baseline_for(int cy, Face face, int size) noexcept
{
    return cy + font::cap_height(face, size) / 2;
}

void text_right(Canvas &c, float right, float baseline, const char *s, Face face, int size,
                Color color, float tracking) noexcept
{
    c.text(right - Canvas::text_width(s, face, size, tracking), baseline, s, face, size, color, 255,
           tracking);
}

void text_center(Canvas &c, float cx, float baseline, const char *s, Face face, int size,
                 Color color, float tracking) noexcept
{
    c.text(cx - Canvas::text_width(s, face, size, tracking) / 2, baseline, s, face, size, color,
           255, tracking);
}

int paragraph(Canvas &c, float x, float baseline, float w, float line_h, const char *s, Face face,
              int size, Color color) noexcept
{
    char line[256];
    int lines = 0;
    const char *p = s;
    while (*p)
    {
        // Take words while they fit.
        size_t len = 0, fit = 0;
        for (;;)
        {
            size_t end = len;
            while (p[end] == ' ')
                ++end;
            while (p[end] && p[end] != ' ')
                ++end;
            if (end >= sizeof line)
                break;
            memcpy(line, p, end);
            line[end] = '\0';
            if (fit > 0 && Canvas::text_width(line, face, size) > w)
                break;
            fit = end;
            len = end;
            if (!p[end])
                break;
        }
        if (fit == 0)
            break;
        memcpy(line, p, fit);
        line[fit] = '\0';
        c.text(x, baseline + lines * line_h, line, face, size, color);
        ++lines;
        p += fit;
        while (*p == ' ')
            ++p;
    }
    return lines;
}

void card(Canvas &c, int x, int y, int w, int h) noexcept
{
    c.round_rect(x, y, w, h, radius, Color::surface);
    c.round_frame(x, y, w, h, radius, border, Color::line);
}

int card_header(Canvas &c, int x, int y, int w, const char *title, const char *sub) noexcept
{
    const int h = px(52);
    const float base = baseline_for(y + h / 2, Face::semibold, px(14));
    const float tw = c.text(x + px(16), base, title, Face::semibold, px(14), Color::text, 255, -0.01f);
    if (sub && sub[0])
        c.text(x + px(16) + tw + px(10), base, sub, Face::medium, px(12), Color::faint);
    c.rect(x + border, y + h - border, w - 2 * border, border, Color::line);
    return h;
}

void eyebrow(Canvas &c, float x, float baseline, const char *text, Color color) noexcept
{
    char up[64];
    size_t i = 0;
    for (; text[i] && i < sizeof up - 1; ++i)
        up[i] = static_cast<char>(toupper(static_cast<unsigned char>(text[i])));
    up[i] = '\0';
    c.text(x, baseline, up, Face::semibold, px(11), color, 255, 0.08f);
}

void toggle(Canvas &c, int x, int y, bool on) noexcept
{
    c.round_rect(x, y, sw_w, sw_h, sw_h / 2, on ? Color::accent : Color::line2);
    const float knob = px(18) / 2.0f, inset = px(3);
    const float cx = on ? x + sw_w - inset - knob : x + inset + knob;
    c.circle(cx, y + sw_h / 2.0f, knob, on ? Color::white : Color::knob);
}

void selection(Canvas &c, int x, int y, int w, int h) noexcept
{
    c.round_rect(x, y, w, h, px(8), Color::accent, 33); // rgba(142,162,255,.13)
}

int chip(Canvas &c, int x, int y, int h, const char *text, bool primary) noexcept
{
    const int size = px(13);
    const int w = static_cast<int>(Canvas::text_width(text, primary ? Face::semibold : Face::medium, size)) +
                  2 * px(12);
    if (primary)
        c.round_rect(x, y, w, h, radius_sm, Color::accent);
    else
    {
        c.round_rect(x, y, w, h, radius_sm, Color::surface2);
        c.round_frame(x, y, w, h, radius_sm, border, Color::line2);
    }
    c.text(x + px(12), baseline_for(y + h / 2, Face::medium, size), text,
           primary ? Face::semibold : Face::medium, size, primary ? Color::bg : Color::text);
    return w;
}

int segmented(Canvas &c, int x, int cy, const char *const *labels, int n, int active, Face face, int size,
              int pad_x) noexcept
{
    int widths[8], total = 0;
    for (int i = 0; i < n && i < 8; ++i)
        total += widths[i] = static_cast<int>(Canvas::text_width(labels[i], face, size)) + 2 * pad_x;
    if (x < 0)
        return total;
    const int h = px(30), y = cy - h / 2;
    c.round_rect(x, y, total, h, radius_sm, Color::surface2);
    int sx = x;
    for (int i = 0; i < n && i < 8; ++i)
    {
        if (i == active)
        {
            // The bar's own rounded shape, cut to this section: only the
            // outer ends are rounded.
            c.clip(sx, y, widths[i], h);
            c.round_rect(x, y, total, h, radius_sm, Color::accent, 33);
            c.unclip();
        }
        text_center(c, sx + widths[i] / 2.0f, baseline_for(cy, face, size), labels[i], face, size,
                    i == active ? Color::accent : Color::muted);
        sx += widths[i];
    }
    sx = x;
    for (int i = 0; i + 1 < n && i < 8; ++i)
    {
        sx += widths[i];
        c.rect(sx - ui::border / 2, y, ui::border, h, Color::line2);
    }
    c.round_frame(x, y, total, h, radius_sm, border, Color::line2);
    return total;
}

int active_badge(Canvas &c, int x, int y, int h) noexcept
{
    const int size = px(13);
    const char *text = "Active";
    const int w = static_cast<int>(Canvas::text_width(text, Face::semibold, size)) + 2 * px(11);
    c.round_rect(x, y, w, h, radius_sm, Color::fan, 26);      // rgba(79,209,165,.1)
    c.round_frame(x, y, w, h, radius_sm, border, Color::fan, 89); // .35
    c.text(x + px(11), baseline_for(y + h / 2, Face::semibold, size), text, Face::semibold, size,
           Color::fan);
    return w;
}

namespace
{
// The PlayStation symbols and D-pad marks, drawn as lines inside a key chip.
void symbol(Canvas &c, float cx, float cy, Key k, Color col) noexcept
{
    const float s = px(5.2f), t = 2.6f;
    switch (k)
    {
    case Key::cross: {
        const float a[] = {cx - s, cy - s, cx + s, cy + s};
        const float b[] = {cx - s, cy + s, cx + s, cy - s};
        c.polyline(a, 2, t, col);
        c.polyline(b, 2, t, col);
        break;
    }
    case Key::circle:
        c.ring(cx, cy, s, t, col);
        break;
    case Key::square: {
        const float a[] = {cx - s, cy - s, cx + s, cy - s, cx + s, cy + s, cx - s, cy + s, cx - s, cy - s};
        c.polyline(a, 5, t, col);
        break;
    }
    case Key::triangle: {
        const float h = s * 1.1f;
        const float a[] = {cx, cy - h, cx + s * 1.15f, cy + h * 0.75f, cx - s * 1.15f, cy + h * 0.75f, cx, cy - h};
        c.polyline(a, 4, t, col);
        break;
    }
    case Key::left_right: {
        const float l[] = {cx - s * 0.55f, cy - s, cx - s * 1.5f, cy, cx - s * 0.55f, cy + s};
        const float r[] = {cx + s * 0.55f, cy - s, cx + s * 1.5f, cy, cx + s * 0.55f, cy + s};
        c.polyline(l, 3, t, col);
        c.polyline(r, 3, t, col);
        break;
    }
    case Key::touchpad: {
        // The pad seen from above: a wide rounded outline.
        const float w = s * 2.1f, h = s * 1.2f;
        const float a[] = {cx - w, cy - h, cx + w, cy - h, cx + w, cy + h, cx - w, cy + h, cx - w, cy - h};
        c.polyline(a, 5, t, col);
        break;
    }
    case Key::up_down: {
        const float u[] = {cx - s, cy - s * 0.45f, cx, cy - s * 1.35f, cx + s, cy - s * 0.45f};
        const float d[] = {cx - s, cy + s * 0.45f, cx, cy + s * 1.35f, cx + s, cy + s * 0.45f};
        c.polyline(u, 3, t, col);
        c.polyline(d, 3, t, col);
        break;
    }
    default:
        break;
    }
}

const char *key_text(Key k) noexcept
{
    switch (k)
    {
    case Key::l1:
        return "L1";
    case Key::r1:
        return "R1";
    case Key::l2_r2:
        return "L2 R2";
    case Key::l1_r1:
        return "L1 R1";
    case Key::options:
        return "OPTIONS";
    case Key::l3:
        return "L3";
    case Key::r3:
        return "R3";
    default:
        return nullptr;
    }
}
} // namespace

int key(Canvas &c, int x, int cy, Key k) noexcept
{
    const int h = px(19), size = px(10.5f);
    const char *label = key_text(k);
    const int w = label ? static_cast<int>(Canvas::text_width(label, Face::mono_medium, size)) + 2 * px(6)
                        : (k == Key::left_right || k == Key::up_down ? px(24) : k == Key::touchpad ? px(30) : h);
    const int y = cy - h / 2;
    // <kbd>: a line2 border with a heavier bottom edge.
    c.round_rect(x, y, w, h, px(5), Color::line2);
    c.round_rect(x + border, y + border, w - 2 * border, h - 2 * border - 2, px(5) - border,
                 Color::surface);
    if (label)
        c.text(x + px(6), baseline_for(cy - 1, Face::mono_medium, size), label, Face::mono_medium, size,
               Color::muted);
    else
        symbol(c, x + w / 2.0f, cy - 1.0f, k, Color::muted);
    return w;
}

int key_hint(Canvas &c, int x, int cy, Key k, const char *label) noexcept
{
    const int w = key(c, x, cy, k);
    const float lw = c.text(x + w + px(7), baseline_for(cy, Face::medium, px(12.5f)), label, Face::medium,
                            px(12.5f), Color::muted);
    return w + px(7) + static_cast<int>(lw);
}

void hint_bar(Canvas &c, const Hint *hints, int count) noexcept
{
    int x = px(41);
    const int cy = screen::height - px(30);
    for (int i = 0; i < count; ++i)
        x += key_hint(c, x, cy, hints[i].key, hints[i].label) + px(22);
}
} // namespace breeze::ui
