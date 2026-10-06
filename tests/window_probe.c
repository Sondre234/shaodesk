// SPDX-License-Identifier: GPL-3.0-or-later
#include "ext-image-capture-source-v1-client-protocol.h" // before the next, which names its interface
#include "shaodesk-window-control-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

/* A taskbar's view of one window through shaodesk-window-control-v1, for the tests: finds the
 * window titled TITLE through wlr-foreign-toplevel, then prints its state, sends one request,
 * or prints each state the compositor sends until the window closes.
 *
 *   window_probe TITLE                   prints "OUTPUT WORKSPACE STATE"
 *   window_probe TITLE watch             prints that line after every done, until it closes
 *   window_probe TITLE workspace N       move_to_workspace
 *   window_probe TITLE output NAME       move_to_output
 *   window_probe TITLE sticky 0|1        set_sticky or unset_sticky
 *   window_probe TITLE floating 0|1      set_floating or unset_floating
 *
 * STATE is the flags joined by commas ("floating,tiling"), or "-" for none; OUTPUT is "-" before
 * the window is on one. */
#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct handle {
    struct zwlr_foreign_toplevel_handle_v1 *object;
    char *title;
    struct handle *next;
};
struct probe {
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct shaodesk_window_control_v1 *control;
    struct handle *handles;
    bool closed, watch;
    char output[64];
    uint32_t workspace, state;
};

static void die(const char *message) {
    fprintf(stderr, "window probe: %s\n", message);
    exit(1);
}

static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                         const char *title) {
    struct handle *handle = data;
    free(handle->title);
    handle->title = strdup(title);
}
static void handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          const char *app_id) {}
static void handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          struct wl_output *output) {}
static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                         struct wl_array *states) {}
static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *object) {}
static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *object) {
    struct handle *handle = data;
    free(handle->title);
    handle->title = NULL; // matches no title any more
}
static void handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          struct zwlr_foreign_toplevel_handle_v1 *parent) {}
static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
    .title = handle_title,
    .app_id = handle_app_id,
    .output_enter = handle_output,
    .output_leave = handle_output,
    .state = handle_state,
    .done = handle_done,
    .closed = handle_closed,
    .parent = handle_parent};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
                             struct zwlr_foreign_toplevel_handle_v1 *object) {
    struct probe *probe = data;
    struct handle *handle = calloc(1, sizeof(*handle));
    if (!handle)
        die("out of memory");
    handle->object = object;
    handle->next = probe->handles;
    probe->handles = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(object, &handle_listener, handle);
}
static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {}
static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_toplevel, .finished = manager_finished};

static void print_state(struct probe *probe) {
    static const char *names[] = {"sticky", "floating", "tiled", "tiling"};
    char state[64] = "";
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (probe->state & 1u << i)
            snprintf(state + strlen(state), sizeof(state) - strlen(state), "%s%s",
                     state[0] ? "," : "", names[i]);
    printf("%s %u %s\n", probe->output[0] ? probe->output : "-", probe->workspace,
           state[0] ? state : "-");
    fflush(stdout);
}

static void window_output(void *data, struct shaodesk_window_v1 *window, const char *name) {
    struct probe *probe = data;
    snprintf(probe->output, sizeof(probe->output), "%s", name);
}
static void window_workspace(void *data, struct shaodesk_window_v1 *window, uint32_t number) {
    ((struct probe *)data)->workspace = number;
}
static void window_state(void *data, struct shaodesk_window_v1 *window, uint32_t state) {
    ((struct probe *)data)->state = state;
}
static void window_done(void *data, struct shaodesk_window_v1 *window) {
    struct probe *probe = data;
    if (probe->watch)
        print_state(probe);
}
static const struct shaodesk_window_v1_listener window_listener = {
    .output = window_output,
    .workspace = window_workspace,
    .state = window_state,
    .done = window_done};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        probe->manager =
            wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, 3);
        zwlr_foreign_toplevel_manager_v1_add_listener(probe->manager, &manager_listener, probe);
    } else if (!strcmp(interface, shaodesk_window_control_v1_interface.name)) {
        probe->control = wl_registry_bind(registry, name, &shaodesk_window_control_v1_interface, 1);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_global_remove};

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3 && argc != 4)
        die("usage: window_probe TITLE [watch | workspace N | output NAME | sticky 0|1 | "
            "floating 0|1]");
    const char *title = argv[1], *command = argc > 2 ? argv[2] : "", *argument = argc > 3 ? argv[3] : "";
    struct probe probe = {.watch = !strcmp(command, "watch")};
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    // The globals, then the windows, then their titles.
    for (int i = 0; i < 3; ++i)
        if (wl_display_roundtrip(display) < 0)
            die("roundtrip failed");
    if (!probe.manager || !probe.control)
        die("the compositor offers no foreign-toplevel manager or window control");
    struct handle *found = NULL;
    for (struct handle *handle = probe.handles; handle; handle = handle->next)
        if (handle->title && !strcmp(handle->title, title))
            found = handle;
    if (!found)
        die("no window has that title");
    struct shaodesk_window_v1 *window =
        shaodesk_window_control_v1_get_window(probe.control, found->object);
    shaodesk_window_v1_add_listener(window, &window_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("roundtrip failed");
    if (!strcmp(command, "")) {
        print_state(&probe);
    } else if (probe.watch) { // the first done printed the state
        while (found->title)
            if (wl_display_dispatch(display) < 0)
                die("dispatch failed");
    } else if (!strcmp(command, "workspace") && argument[0]) {
        shaodesk_window_v1_move_to_workspace(window, (uint32_t)strtoul(argument, NULL, 10));
    } else if (!strcmp(command, "output") && argument[0]) {
        shaodesk_window_v1_move_to_output(window, argument);
    } else if (!strcmp(command, "sticky") && argument[0]) {
        if (!strcmp(argument, "1"))
            shaodesk_window_v1_set_sticky(window);
        else
            shaodesk_window_v1_unset_sticky(window);
    } else if (!strcmp(command, "floating") && argument[0]) {
        if (!strcmp(argument, "1"))
            shaodesk_window_v1_set_floating(window);
        else
            shaodesk_window_v1_unset_floating(window);
    } else {
        die("unknown command");
    }
    if (wl_display_roundtrip(display) < 0)
        die("request failed");
    shaodesk_window_v1_destroy(window);
    shaodesk_window_control_v1_destroy(probe.control);
    for (struct handle *handle = probe.handles, *next; handle; handle = next) {
        next = handle->next;
        zwlr_foreign_toplevel_handle_v1_destroy(handle->object);
        free(handle->title);
        free(handle);
    }
    zwlr_foreign_toplevel_manager_v1_destroy(probe.manager);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
