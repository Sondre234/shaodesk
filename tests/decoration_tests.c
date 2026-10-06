// SPDX-License-Identifier: GPL-3.0-or-later
/* What the compositor draws for a window, as pixels: the window controls in both styles. */
#include "shaodesk/decoration.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>

static int failures;
#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s: ", __FILE__, __LINE__, #condition);                        \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static int alpha(uint32_t pixel) { return (int)(pixel >> 24); }
static int red(uint32_t pixel) { return (int)((pixel >> 16) & 0xff); }
static int green(uint32_t pixel) { return (int)((pixel >> 8) & 0xff); }
static int blue(uint32_t pixel) { return (int)(pixel & 0xff); }
static int brightness(uint32_t pixel) { return red(pixel) + green(pixel) + blue(pixel); }

/* The controls painted with `look` at `scale`, malloc'd. */
static uint32_t *paint(const struct sh_deco_look *look, int scale, int *width) {
    int w, h;
    sh_decoration_size(look->style, &w, &h);
    uint32_t *pixels = calloc((size_t)w * scale * h * scale, sizeof(*pixels));
    sh_decoration_paint(pixels, scale, look);
    *width = w * scale;
    return pixels;
}

/* The pixel at logical (x, y), taking the one whose top-left corner is there. */
static uint32_t at(const uint32_t *pixels, int width, int scale, double x, double y) {
    return pixels[(int)(y * scale) * width + (int)(x * scale)];
}

static void flat_strip(void) {
    int w, h;
    sh_decoration_size(SH_DECO_FLAT, &w, &h);
    CHECK(w == SH_DECO_WIDTH && h == SH_DECO_HEIGHT, "flat strip is %dx%d", w, h);
    // Minimize, fullscreen, close from the left, the whole strip shared out.
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 1, 1) == SH_DECO_MINIMIZE, "left third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 48, 14) == SH_DECO_FULLSCREEN, "middle third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 95, 27) == SH_DECO_CLOSE, "right third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, -1, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_FLAT, 96, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_FLAT, 50, 28) == SH_DECO_NONE,
          "outside the strip");
    for (int scale = 1; scale <= 2; ++scale) {
        int width;
        struct sh_deco_look look = {SH_DECO_FLAT, true, SH_DECO_NONE, SH_DECO_NONE};
        uint32_t *plain = paint(&look, scale, &width);
        look.hovered = SH_DECO_CLOSE;
        uint32_t *lit = paint(&look, scale, &width);
        // A dark translucent strip with rounded corners; a hovered close button turns red.
        uint32_t background = at(plain, width, scale, 72, 3);
        CHECK(alpha(background) > 200 && alpha(background) < 230 && brightness(background) < 100,
              "strip background %08x at scale %d", background, scale);
        CHECK(alpha(at(plain, width, scale, 0, 0)) == 0, "corner of the strip painted");
        uint32_t close = at(lit, width, scale, 72, 3);
        CHECK(red(close) > 200 && green(close) < 80 && alpha(close) == 255,
              "hovered close %08x at scale %d", close, scale);
        // Focus and a press leave the flat strip as it is.
        look.focused = false;
        look.pressed = SH_DECO_CLOSE;
        uint32_t *other = paint(&look, scale, &width);
        CHECK(!memcmp(other, lit, sizeof(*lit) * width * SH_DECO_HEIGHT * scale),
              "flat strip changed with focus or a press");
        free(plain);
        free(lit);
        free(other);
    }
}

static const struct {
    enum sh_deco_part part;
    int r, g, b;
} lights[3] = {
    {SH_DECO_CLOSE, 0xff, 0x5f, 0x57},
    {SH_DECO_MINIMIZE, 0xfe, 0xbc, 0x2e},
    {SH_DECO_FULLSCREEN, 0x28, 0xc8, 0x40},
};

static void traffic_lights(void) {
    int w, h;
    sh_decoration_size(SH_DECO_TRAFFIC_LIGHTS, &w, &h);
    CHECK(w == SH_LIGHTS_WIDTH && h == SH_LIGHTS_HEIGHT, "traffic lights are %dx%d", w, h);
    // Close, minimize, fullscreen from the left; each takes the pointer on and just around its
    // circle, and halfway to the next.
    for (int i = 0; i < 3; ++i) {
        double cx = SH_LIGHTS_INSET + i * SH_LIGHTS_SPACING, cy = SH_LIGHTS_INSET;
        CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, cx, cy) == lights[i].part,
              "light %d not at its centre", i);
        CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, cx + 7, cy - 7) == lights[i].part,
              "light %d not just around its circle", i);
    }
    CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 23.9, 14) == SH_DECO_CLOSE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 24.1, 14) == SH_DECO_MINIMIZE,
          "the gap between two lights is not split at its middle");
    CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 14, 24) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 14, 3) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 3, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 65, 14) == SH_DECO_NONE,
          "the lights take the pointer far from their circles");

    for (int scale = 1; scale <= 2; ++scale) {
        int width;
        struct sh_deco_look look = {SH_DECO_TRAFFIC_LIGHTS, true, SH_DECO_NONE, SH_DECO_NONE};
        uint32_t *focused = paint(&look, scale, &width);
        look.focused = false;
        uint32_t *unfocused = paint(&look, scale, &width);
        look.hovered = SH_DECO_MINIMIZE;
        uint32_t *hovered = paint(&look, scale, &width);
        look.pressed = SH_DECO_MINIMIZE;
        uint32_t *pressed = paint(&look, scale, &width);
        for (int i = 0; i < 3; ++i) {
            double cx = SH_LIGHTS_INSET + i * SH_LIGHTS_SPACING, cy = SH_LIGHTS_INSET;
            // Their colours while focused, opaque; a point off the symbol stands for the fill.
            uint32_t fill = at(focused, width, scale, cx + 3, cy - 2);
            CHECK(alpha(fill) == 255 && abs(red(fill) - lights[i].r) <= 2 &&
                      abs(green(fill) - lights[i].g) <= 2 && abs(blue(fill) - lights[i].b) <= 2,
                  "light %d is %08x at scale %d", i, fill, scale);
            // Grey and translucent without focus.
            uint32_t grey = at(unfocused, width, scale, cx + 3, cy - 2);
            CHECK(alpha(grey) > 60 && alpha(grey) < 200 && abs(red(grey) - green(grey)) <= 3 &&
                      abs(green(grey) - blue(grey)) <= 6,
                  "unfocused light %d is %08x at scale %d", i, grey, scale);
            // Hovering one colours all three and shows their symbols: the middle of each is
            // darker than its fill, except fullscreen's, whose triangles leave the centre clear.
            uint32_t lit = at(hovered, width, scale, cx + 3, cy - 2);
            CHECK(abs(red(lit) - lights[i].r) <= 2 && abs(green(lit) - lights[i].g) <= 2,
                  "hovered light %d is %08x at scale %d", i, lit, scale);
            double gx = lights[i].part == SH_DECO_FULLSCREEN ? cx - 2 : cx;
            double gy = lights[i].part == SH_DECO_FULLSCREEN ? cy - 2 : cy;
            CHECK(brightness(at(hovered, width, scale, gx, gy)) < brightness(lit) - 150,
                  "light %d shows no symbol at scale %d", i, scale);
            CHECK(brightness(at(focused, width, scale, gx, gy)) > brightness(lit) - 30,
                  "light %d shows its symbol unhovered at scale %d", i, scale);
            // Only the pressed one darkens.
            uint32_t held = at(pressed, width, scale, cx + 3, cy - 2);
            if (lights[i].part == SH_DECO_MINIMIZE)
                CHECK(brightness(held) < brightness(lit) - 60, "pressed light not darker");
            else
                CHECK(held == lit, "light %d changed by another's press", i);
        }
        // Around and between the circles nothing is drawn.
        for (double x = 0; x < SH_LIGHTS_WIDTH; x += 0.5) {
            CHECK(alpha(at(focused, width, scale, x, 1)) == 0, "drawn above the lights");
            CHECK(alpha(at(focused, width, scale, x, SH_LIGHTS_HEIGHT - 1)) == 0,
                  "drawn below the lights");
        }
        CHECK(alpha(at(focused, width, scale, 24, 14)) == 0 &&
                  alpha(at(focused, width, scale, 44, 14)) == 0 &&
                  alpha(at(focused, width, scale, 66, 14)) == 0,
              "drawn between or after the lights at scale %d", scale);
        free(focused);
        free(unfocused);
        free(hovered);
        free(pressed);
    }
}

/* A rendered buffer holds what painting gives, at the scale asked for. */
static void rendered_buffer(void) {
    struct sh_deco_look look = {SH_DECO_TRAFFIC_LIGHTS, true, SH_DECO_CLOSE, SH_DECO_CLOSE};
    struct wlr_buffer *buffer = sh_decoration_render(2, &look);
    CHECK(buffer && buffer->width == 2 * SH_LIGHTS_WIDTH && buffer->height == 2 * SH_LIGHTS_HEIGHT,
          "buffer size");
    if (!buffer)
        return;
    int width;
    uint32_t *expected = paint(&look, 2, &width);
    void *data;
    uint32_t format;
    size_t stride;
    CHECK(wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format,
                                           &stride),
          "buffer not readable");
    CHECK(stride == (size_t)width * 4 &&
              !memcmp(data, expected, stride * (size_t)buffer->height),
          "buffer differs from the painted pixels");
    wlr_buffer_end_data_ptr_access(buffer);
    wlr_buffer_drop(buffer);
    free(expected);
}

int main(void) {
    flat_strip();
    traffic_lights();
    rendered_buffer();
    if (failures) {
        fprintf(stderr, "%d decoration checks failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("Window controls passed\n");
    return EXIT_SUCCESS;
}
