/*
 * Breeze app - full-screen CPU drawing on the PS5's VideoOut, antialiased,
 * with the web panel's fonts. Based on ps5-native-app-boilerplate's demo
 * renderer, Copyright (C) 2026 BlackBearReloaded; changes for Breeze.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "font.hpp"

#include <cstdint>
#include <string_view>

namespace breeze::screen
{
// 0xAABBGGRR (the alpha byte is ignored: opacity is a separate argument).
// The web panel's palette.
enum class Color : std::uint32_t
{
    bg = UINT32_C(0xff100d0b),
    surface = UINT32_C(0xff1a1512),
    surface2 = UINT32_C(0xff211b17),
    hover = UINT32_C(0xff28211c), // #1c2128, tips and toasts
    line = UINT32_C(0xff2e2620),
    line2 = UINT32_C(0xff3c322b),
    grid_minor = UINT32_C(0xff1f1915), // #15191f
    grid_major = UINT32_C(0xff29211c), // #1c2129
    text = UINT32_C(0xfff1ece9),
    muted = UINT32_C(0xffa5968d),
    faint = UINT32_C(0xff73655c),
    accent = UINT32_C(0xffffa28e),
    fan = UINT32_C(0xffa5d14f),
    temp = UINT32_C(0xff5cb2f2),
    danger = UINT32_C(0xff7a77f2),
    knob = UINT32_C(0xffded5cf), // #cfd5de
    white = UINT32_C(0xffffffff),
};

constexpr int width = 1920;
constexpr int height = 1080;

class Canvas;
using Update = bool (*)() noexcept;
using Draw = void (*)(Canvas &) noexcept;

// Calls update about 60 times a second and draws the scene again whenever it
// returns true. Never returns: the system closes the app.
[[noreturn]] void run(Update update, Draw draw) noexcept;

// For building on a PC: draws one frame and writes it as a PPM image.
void preview(Draw draw, const char *ppm_path) noexcept;

// Draws into a plain buffer in RAM; run() copies it to the screen. Opacity is
// 0..255, sizes are pixels at 1920x1080, shapes are antialiased and clipped.
class Canvas final
{
  public:
    void clear(Color c) noexcept;
    // Everything drawn after clip() stays inside the box, until unclip().
    void clip(int x, int y, int w, int h) noexcept;
    void unclip() noexcept;
    void rect(int x, int y, int w, int h, Color c, int alpha = 255) noexcept;
    void round_rect(int x, int y, int w, int h, int r, Color c, int alpha = 255) noexcept;
    void round_frame(int x, int y, int w, int h, int r, int thick, Color c,
                     int alpha = 255) noexcept;
    void circle(float cx, float cy, float r, Color c, int alpha = 255) noexcept;
    void ring(float cx, float cy, float r, float thick, Color c, int alpha = 255) noexcept;
    // A thick line through xy[0..2n), round joins and caps; dash > 0 draws it dashed.
    void polyline(const float *xy, int n, float thick, Color c, int alpha = 255, float dash = 0,
                  float gap = 0) noexcept;
    // Fills from the polyline down to floor_y, fading from alpha_top at its
    // highest point to nothing at floor_y.
    void fill_under(const float *xy, int n, int floor_y, Color c, int alpha_top) noexcept;
    void dashed_h(int x0, int x1, int y, int thick, int dash, int gap, Color c,
                  int alpha = 255) noexcept;
    void dashed_v(int x, int y0, int y1, int thick, int dash, int gap, Color c,
                  int alpha = 255) noexcept;
    // An icon: c painted through a w x h coverage mask (0..255, row by row).
    void icon(int x, int y, int w, int h, const std::uint8_t *cover, Color c, int alpha = 255) noexcept;
    // Text with its baseline at y; tracking in em (CSS letter-spacing). Returns
    // the width drawn.
    float text(float x, float y, std::string_view s, font::Face face, int px, Color c,
               int alpha = 255, float tracking = 0) noexcept;
    [[nodiscard]] static float text_width(std::string_view s, font::Face face, int px,
                                          float tracking = 0) noexcept;

  private:
    Canvas() noexcept = default;
    friend void run(Update update, Draw draw) noexcept;
    friend void preview(Draw draw, const char *ppm_path) noexcept;
};

// Milliseconds since some fixed point, for timing.
[[nodiscard]] std::uint64_t now_ms() noexcept;

// A system notification (the pop-up in the top corner).
void notify(std::string_view message) noexcept;
} // namespace breeze::screen
