// SPDX-License-Identifier: GPL-3.0-or-later
/* A window that prints the input it gets, a line per event, for the input tests: the pointer
 * entering, moving and pressing buttons over it, touchpad gestures through
 * pointer-gestures-unstable-v1, and fingers on a touchscreen through wl_touch. It prints "ready"
 * once it has drawn, and runs until it is ended.
 *   pointer enter X Y | pointer leave | pointer motion X Y | pointer button CODE pressed|released
 *   swipe begin FINGERS | swipe update DX DY | swipe end CANCELLED
 *   pinch begin FINGERS | pinch update DX DY SCALE ROTATION | pinch end CANCELLED
 *   hold begin FINGERS | hold end CANCELLED
 *   touch down ID X Y | touch motion ID X Y | touch up ID | touch frame | touch cancel
 * Usage: input_probe [--no-gestures] [--no-touch] [--layer] [TITLE]: without the gestures or
 * touch it never binds them, as most applications; with --layer it is a panel along the bottom
 * of the output, 60 pixels high, rather than a window. */
#define _GNU_SOURCE
#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct probe {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct zwp_pointer_gestures_v1 *gestures;
    uint32_t gestures_version;
    bool want_gestures;
    struct wl_touch *touch;
    bool want_touch;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    int width, height;
    bool ready;
};

static void die(const char *message) {
    fprintf(stderr, "input probe: %s\n", message);
    exit(1);
}

/* A line on standard output, at once: the test reads it while the probe runs. */
__attribute__((format(printf, 1, 2))) static void say(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    say("pointer enter %.0f %.0f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    say("pointer leave");
}
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x,
                           wl_fixed_t y) {
    say("pointer motion %.0f %.0f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
    say("pointer button %u %s", button,
        state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");
}
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {}
static void pointer_frame(void *data, struct wl_pointer *pointer) {}
static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis) {}
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete) {}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void swipe_begin(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t serial,
                        uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("swipe begin %u", fingers);
}
static void swipe_update(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t time,
                         wl_fixed_t dx, wl_fixed_t dy) {
    say("swipe update %.1f %.1f", wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}
static void swipe_end(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t serial,
                      uint32_t time, int32_t cancelled) {
    say("swipe end %d", cancelled);
}
static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
    .begin = swipe_begin, .update = swipe_update, .end = swipe_end};

static void pinch_begin(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial,
                        uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("pinch begin %u", fingers);
}
static void pinch_update(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t time,
                         wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation) {
    say("pinch update %.1f %.1f %.2f %.1f", wl_fixed_to_double(dx), wl_fixed_to_double(dy),
        wl_fixed_to_double(scale), wl_fixed_to_double(rotation));
}
static void pinch_end(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial,
                      uint32_t time, int32_t cancelled) {
    say("pinch end %d", cancelled);
}
static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
    .begin = pinch_begin, .update = pinch_update, .end = pinch_end};

static void hold_begin(void *data, struct zwp_pointer_gesture_hold_v1 *hold, uint32_t serial,
                       uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("hold begin %u", fingers);
}
static void hold_end(void *data, struct zwp_pointer_gesture_hold_v1 *hold, uint32_t serial,
                     uint32_t time, int32_t cancelled) {
    say("hold end %d", cancelled);
}
static const struct zwp_pointer_gesture_hold_v1_listener hold_listener = {.begin = hold_begin,
                                                                          .end = hold_end};

static void touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                       struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    say("touch down %d %.0f %.0f", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                     int32_t id) {
    say("touch up %d", id);
}
static void touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id,
                         wl_fixed_t x, wl_fixed_t y) {
    say("touch motion %d %.0f %.0f", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touch_frame(void *data, struct wl_touch *touch) {
    say("touch frame");
}
static void touch_cancel(void *data, struct wl_touch *touch) {
    say("touch cancel");
}
static const struct wl_touch_listener touch_listener = {.down = touch_down,
                                                        .up = touch_up,
                                                        .motion = touch_motion,
                                                        .frame = touch_frame,
                                                        .cancel = touch_cancel};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct probe *probe = data;
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !probe->touch && probe->want_touch) {
        probe->touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(probe->touch, &touch_listener, probe);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && probe->touch) {
        wl_touch_release(probe->touch);
        probe->touch = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !probe->pointer) {
        probe->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(probe->pointer, &pointer_listener, probe);
        if (probe->gestures && probe->want_gestures) {
            zwp_pointer_gesture_swipe_v1_add_listener(
                zwp_pointer_gestures_v1_get_swipe_gesture(probe->gestures, probe->pointer),
                &swipe_listener, probe);
            zwp_pointer_gesture_pinch_v1_add_listener(
                zwp_pointer_gestures_v1_get_pinch_gesture(probe->gestures, probe->pointer),
                &pinch_listener, probe);
            if (probe->gestures_version >= 3)
                zwp_pointer_gesture_hold_v1_add_listener(
                    zwp_pointer_gestures_v1_get_hold_gesture(probe->gestures, probe->pointer),
                    &hold_listener, probe);
        }
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = {.capabilities = seat_capabilities,
                                                      .name = seat_name};

static void shell_ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = shell_ping};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        probe->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        probe->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(probe->shell, &shell_listener, probe);
    } else if (!strcmp(interface, wl_seat_interface.name) && !probe->seat) {
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(probe->seat, &seat_listener, probe);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        probe->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, zwp_pointer_gestures_v1_interface.name)) {
        probe->gestures_version = version < 3 ? version : 3;
        probe->gestures = wl_registry_bind(registry, name, &zwp_pointer_gestures_v1_interface,
                                           probe->gestures_version);
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

/* A buffer of the window's size in one colour. */
static struct wl_buffer *make_buffer(struct probe *probe) {
    size_t size = (size_t)probe->width * probe->height * 4;
    int fd = memfd_create("shaodesk-input-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0)
        die("cannot allocate a buffer");
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map a buffer");
    for (size_t i = 0; i < size / 4; ++i)
        pixels[i] = 0xff6a8f3c;
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int32_t)size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, probe->width, probe->height,
                                                         probe->width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct probe *probe = data;
    wl_callback_destroy(callback);
    if (!probe->ready) {
        probe->ready = true;
        say("ready");
    }
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};

static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct probe *probe = data;
    xdg_surface_ack_configure(surface, serial);
    wl_surface_attach(probe->surface, make_buffer(probe), 0, 0);
    wl_surface_damage_buffer(probe->surface, 0, 0, probe->width, probe->height);
    if (!probe->ready)
        wl_callback_add_listener(wl_surface_frame(probe->surface), &frame_listener, probe);
    wl_surface_commit(probe->surface);
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {
    struct probe *probe = data;
    probe->width = width > 0 ? width : 400;
    probe->height = height > 0 ? height : 300;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) { exit(0); }
static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure,
                                                               .close = toplevel_close};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial,
                            uint32_t width, uint32_t height) {
    struct probe *probe = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    probe->width = width > 0 && width <= 8192 ? (int)width : 400;
    probe->height = height > 0 && height <= 8192 ? (int)height : 60;
    wl_surface_attach(probe->surface, make_buffer(probe), 0, 0);
    wl_surface_damage_buffer(probe->surface, 0, 0, probe->width, probe->height);
    if (!probe->ready)
        wl_callback_add_listener(wl_surface_frame(probe->surface), &frame_listener, probe);
    wl_surface_commit(probe->surface);
}
static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) { exit(0); }
static const struct zwlr_layer_surface_v1_listener layer_listener = {.configure = layer_configure,
                                                                     .closed = layer_closed};

int main(int argc, char **argv) {
    struct probe probe = {.want_gestures = true, .want_touch = true, .width = 400, .height = 300};
    const char *title = "input probe";
    bool layer = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-gestures"))
            probe.want_gestures = false;
        else if (!strcmp(argv[i], "--no-touch"))
            probe.want_touch = false;
        else if (!strcmp(argv[i], "--layer"))
            layer = true;
        else
            title = argv[i];
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("registry roundtrip failed");
    if (!probe.compositor || !probe.shm || !probe.shell || !probe.seat)
        die("required globals missing");
    if (probe.want_gestures && !probe.gestures)
        die("zwp_pointer_gestures_v1 is not offered");
    probe.surface = wl_compositor_create_surface(probe.compositor);
    if (layer) {
        if (!probe.layer_shell)
            die("zwlr_layer_shell_v1 is not offered");
        probe.layer = zwlr_layer_shell_v1_get_layer_surface(
            probe.layer_shell, probe.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, title);
        zwlr_layer_surface_v1_add_listener(probe.layer, &layer_listener, &probe);
        zwlr_layer_surface_v1_set_size(probe.layer, 0, 60);
        zwlr_layer_surface_v1_set_anchor(probe.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(probe.layer, 60);
    } else {
        probe.xdg_surface = xdg_wm_base_get_xdg_surface(probe.shell, probe.surface);
        xdg_surface_add_listener(probe.xdg_surface, &surface_listener, &probe);
        probe.toplevel = xdg_surface_get_toplevel(probe.xdg_surface);
        xdg_toplevel_add_listener(probe.toplevel, &toplevel_listener, &probe);
        xdg_toplevel_set_title(probe.toplevel, title);
        xdg_toplevel_set_app_id(probe.toplevel, "input-probe");
    }
    wl_surface_commit(probe.surface);
    while (wl_display_dispatch(display) >= 0)
        ;
    die("the connection broke");
}
