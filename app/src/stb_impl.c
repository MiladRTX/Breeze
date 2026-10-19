/*
 * Breeze app - stb_truetype's implementation (public domain / MIT, by Sean
 * Barrett and contributors), compiled once here.
 */
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"

/* The app's runtime has no __assert; the checks only guard against broken
 * font files, and the app ships its own. */
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
