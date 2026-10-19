/*
 * Breeze app - the web panel's fonts (Inter, JetBrains Mono), rasterised with
 * stb_truetype and cached per face, size and character.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace breeze::font
{
enum Face
{
    regular,
    medium,
    semibold,
    bold,
    mono_medium,
    mono_semibold,
    face_count,
};

struct Glyph
{
    int w, h;       // bitmap size
    int xoff, yoff; // from the pen position on the baseline to the bitmap's top left
    float advance;  // in pixels
    const std::uint8_t *bits; // w * h coverage, nullptr for an empty glyph
};

// Reads the faces from dir (with a trailing slash). False if any is missing.
bool load(const char *dir) noexcept;

// nullptr when the face isn't loaded; a glyph without bits for blanks.
[[nodiscard]] const Glyph *glyph(Face face, int px, std::uint32_t codepoint) noexcept;
[[nodiscard]] float kern(Face face, int px, std::uint32_t left, std::uint32_t right) noexcept;
// Distance from the baseline up to the top of capital letters.
[[nodiscard]] float cap_height(Face face, int px) noexcept;

// The next UTF-8 code point at s[at], moving at past it.
[[nodiscard]] std::uint32_t next(const char *s, int len, int &at) noexcept;
} // namespace breeze::font
