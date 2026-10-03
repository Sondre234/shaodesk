// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Window controls for windows that ask the compositor to decorate them: three flat buttons
 * (minimize, fullscreen, close from left to right) with thin glyphs on a dark translucent strip
 * over the window's top-right corner. */
#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;

enum sh_deco_part {
    SH_DECO_NONE,
    SH_DECO_CLOSE,
    SH_DECO_MINIMIZE,
    SH_DECO_FULLSCREEN,
};

/* Logical size of the strip and its inset from the window's top-right corner. */
enum { SH_DECO_MARGIN = 6, SH_DECO_WIDTH = 96, SH_DECO_HEIGHT = 28 };

/* Renders the strip at `scale` pixels per logical pixel with the `hovered` button lit up
 * (SH_DECO_NONE for none). Returns NULL when out of memory. */
struct wlr_buffer *sh_decoration_render(int scale, enum sh_deco_part hovered);

/* The part at strip-local logical coordinates. */
enum sh_deco_part sh_decoration_part_at(double x, double y);

/* Wraps premultiplied ARGB pixels (malloc'd, `width` by `height`) in a buffer that frees them
 * with its last reference. Returns NULL, freeing them, when out of memory. */
struct wlr_buffer *sh_pixel_buffer(uint32_t *pixels, int width, int height);
