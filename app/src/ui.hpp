/*
 * Breeze app - the web panel's components (cards, eyebrow labels, switches,
 * buttons, key hints), drawn at TV size: the panel's CSS pixels times 1.75.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "screen.hpp"

namespace breeze::ui
{
using font::Face;
using screen::Canvas;
using screen::Color;

constexpr float scale = 1.75f;
[[nodiscard]] constexpr int px(float css) noexcept
{
    return static_cast<int>(css * scale + 0.5f);
}

constexpr int radius = px(14), radius_sm = px(9), border = 2;
constexpr int sw_w = px(42), sw_h = px(24);

// .card: surface, a 1px line border, rounded.
void card(Canvas &c, int x, int y, int w, int h) noexcept;
// .hd: a title, a faint sub line, a line under it; returns the header's height.
int card_header(Canvas &c, int x, int y, int w, const char *title, const char *sub) noexcept;
// .eyebrow: small capitals with wide letter spacing.
void eyebrow(Canvas &c, float x, float baseline, const char *text, Color color = Color::faint) noexcept;
// .sw: the panel's slide switch, top left at x, y.
void toggle(Canvas &c, int x, int y, bool on) noexcept;
// A selected row or item: the accent's dim background, rounded.
void selection(Canvas &c, int x, int y, int w, int h) noexcept;
// A button-like chip with text, the primary one filled with the accent.
int chip(Canvas &c, int x, int y, int h, const char *text, bool primary) noexcept;
// .seg: buttons in one bordered bar; the active one's whole section is filled
// with the accent's dim colour. Returns the bar's width; x < 0 only measures.
int segmented(Canvas &c, int x, int cy, const char *const *labels, int n, int active, Face face, int size,
              int pad_x) noexcept;
// The green "Active" badge of a preset.
int active_badge(Canvas &c, int x, int y, int h) noexcept;

enum class Key
{
    cross,
    circle,
    square,
    triangle,
    left_right,
    up_down,
    l1,
    r1,
    l2_r2,
    l1_r1,
    options,
    touchpad,
    l3,
    r3,
};
// A <kbd> chip for a controller button; cy is its vertical centre. Returns its width.
int key(Canvas &c, int x, int cy, Key k) noexcept;
// A key chip and a label after it; returns the total width.
int key_hint(Canvas &c, int x, int cy, Key k, const char *label) noexcept;
// A row of key hints along the bottom of the screen.
struct Hint
{
    Key key;
    const char *label;
};
void hint_bar(Canvas &c, const Hint *hints, int count) noexcept;

// Word-wrapped text within width w; returns the number of lines.
int paragraph(Canvas &c, float x, float baseline, float w, float line_h, const char *s, Face face,
              int px, Color color) noexcept;

// Text helpers: vertical centring on the capital height, right and centre alignment.
float baseline_for(int cy, Face face, int px) noexcept;
void text_right(Canvas &c, float right, float baseline, const char *s, Face face, int px, Color color,
                float tracking = 0) noexcept;
void text_center(Canvas &c, float cx, float baseline, const char *s, Face face, int px, Color color,
                 float tracking = 0) noexcept;
} // namespace breeze::ui
