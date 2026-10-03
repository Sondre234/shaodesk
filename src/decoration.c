// SPDX-License-Identifier: GPL-3.0-or-later
#include "shaode/decoration.h"
#include <drm_fourcc.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>

enum { BUTTON_WIDTH = SH_DECO_WIDTH / 3, CORNER_RADIUS = 6, SAMPLES = 4 };

/* Laid out minimize, fullscreen, close so close is outermost. */
static enum sh_deco_part button_at(double x) {
    static const enum sh_deco_part order[3] = {SH_DECO_MINIMIZE, SH_DECO_FULLSCREEN,
                                               SH_DECO_CLOSE};
    int i = (int)(x / BUTTON_WIDTH);
    return order[i < 0 ? 0 : i > 2 ? 2 : i];
}

struct pixel_buffer {
    struct wlr_buffer base;
    uint32_t *data;
    size_t stride;
};

static void pixel_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct pixel_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    wlr_buffer_finish(wlr_buffer);
    free(buffer->data);
    free(buffer);
}

static bool pixel_buffer_begin(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data,
                               uint32_t *format, size_t *stride) {
    struct pixel_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;
    *data = buffer->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = buffer->stride;
    return true;
}

static void pixel_buffer_end(struct wlr_buffer *wlr_buffer) {}

static const struct wlr_buffer_impl pixel_buffer_impl = {
    .destroy = pixel_buffer_destroy,
    .begin_data_ptr_access = pixel_buffer_begin,
    .end_data_ptr_access = pixel_buffer_end,
};

struct rgba {
    double r, g, b, a; // premultiplied
};

static struct rgba color(uint32_t rgb, double alpha) {
    return (struct rgba){((rgb >> 16) & 0xff) / 255.0 * alpha, ((rgb >> 8) & 0xff) / 255.0 * alpha,
                         (rgb & 0xff) / 255.0 * alpha, alpha};
}

static void over(struct rgba *dst, struct rgba src) {
    dst->r = src.r + dst->r * (1 - src.a);
    dst->g = src.g + dst->g * (1 - src.a);
    dst->b = src.b + dst->b * (1 - src.a);
    dst->a = src.a + dst->a * (1 - src.a);
}

static double segment_distance(double x, double y, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0;
    double t = ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(x - (x0 + t * dx), y - (y0 + t * dy));
}

/* Whether `part`'s glyph covers button-local point (x, y), measured from the centre. */
static bool glyph_covers(enum sh_deco_part part, double x, double y) {
    const double half = 0.5, reach = 5;
    switch (part) {
    case SH_DECO_CLOSE: // ×
        return segment_distance(x, y, -reach, -reach, reach, reach) < half + 0.1 ||
               segment_distance(x, y, -reach, reach, reach, -reach) < half + 0.1;
    case SH_DECO_MINIMIZE: // −, a pixel row below centre to stay sharp
        return fabs(y - half) < half && fabs(x) < reach;
    default: // □
        return fmax(fabs(x), fabs(y)) < reach && fmax(fabs(x), fabs(y)) > reach - 2 * half;
    }
}

/* Whether (x, y) is inside the strip, a rectangle with rounded corners. */
static bool in_strip(double x, double y) {
    double cx = x < CORNER_RADIUS                   ? CORNER_RADIUS
                : x > SH_DECO_WIDTH - CORNER_RADIUS ? SH_DECO_WIDTH - CORNER_RADIUS
                                                    : x;
    double cy = y < CORNER_RADIUS                    ? CORNER_RADIUS
                : y > SH_DECO_HEIGHT - CORNER_RADIUS ? SH_DECO_HEIGHT - CORNER_RADIUS
                                                     : y;
    return hypot(x - cx, y - cy) <= CORNER_RADIUS;
}

static struct rgba sample(double x, double y, enum sh_deco_part hovered) {
    struct rgba pixel = {0};
    if (!in_strip(x, y))
        return pixel;
    over(&pixel, color(0x1e1f22, 0.85));
    enum sh_deco_part part = button_at(x);
    bool lit = part == hovered;
    if (lit)
        over(&pixel, part == SH_DECO_CLOSE ? color(0xda373c, 1) : color(0xffffff, 0.1));
    int column = (int)(x / BUTTON_WIDTH);
    double gx = x - (column + 0.5) * BUTTON_WIDTH, gy = y - SH_DECO_HEIGHT / 2.0;
    if (glyph_covers(part, gx, gy))
        over(&pixel, color(lit ? 0xffffff : 0xb5bac1, 1));
    return pixel;
}

struct wlr_buffer *sh_decoration_render(int scale, enum sh_deco_part hovered) {
    if (scale < 1)
        scale = 1;
    int width = SH_DECO_WIDTH * scale, height = SH_DECO_HEIGHT * scale;
    struct pixel_buffer *buffer = calloc(1, sizeof(*buffer));
    uint32_t *data = calloc((size_t)width * height, sizeof(*data));
    if (!buffer || !data) {
        free(buffer);
        free(data);
        return NULL;
    }
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            struct rgba sum = {0};
            for (int sy = 0; sy < SAMPLES; ++sy) {
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    struct rgba s = sample((px + (sx + 0.5) / SAMPLES) / scale,
                                           (py + (sy + 0.5) / SAMPLES) / scale, hovered);
                    sum.r += s.r, sum.g += s.g, sum.b += s.b, sum.a += s.a;
                }
            }
            double n = SAMPLES * SAMPLES;
            data[py * width + px] =
                (uint32_t)lround(sum.a / n * 255) << 24 | (uint32_t)lround(sum.r / n * 255) << 16 |
                (uint32_t)lround(sum.g / n * 255) << 8 | (uint32_t)lround(sum.b / n * 255);
        }
    }
    buffer->data = data;
    buffer->stride = (size_t)width * sizeof(*data);
    wlr_buffer_init(&buffer->base, &pixel_buffer_impl, width, height);
    return &buffer->base;
}

enum sh_deco_part sh_decoration_part_at(double x, double y) {
    if (x < 0 || y < 0 || x >= SH_DECO_WIDTH || y >= SH_DECO_HEIGHT)
        return SH_DECO_NONE;
    return button_at(x);
}
