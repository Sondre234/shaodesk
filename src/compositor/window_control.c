/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The taskbar's window menu and pictures: shaodesk-window-control-v1 names a window by its
 * wlr-foreign-toplevel handle, tells the shell which output and workspace each is on and how it
 * is placed, moves one to another workspace or output, makes it sticky or floats it, and gives
 * its capture source for a picture of it. */
#include "server.h"

/* One shaodesk_window_v1: the window it names, NULL once that is gone, and what it last sent. */
struct sh_window_object {
    struct wl_resource *resource;
    struct sh_toplevel *toplevel;
    struct wl_list link; // sh_server.window_objects
    bool sent;
    char output[64];
    uint32_t workspace, state, pid;
};

/* The window a taskbar handle the client holds stands for, or NULL when it is gone: the handles
 * wlroots made for a window are the resources of its foreign-toplevel handle. */
static struct sh_toplevel *handle_toplevel(struct sh_server *server, struct wl_resource *handle) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->foreign)
            continue;
        struct wl_resource *resource;
        wl_resource_for_each(resource, &toplevel->foreign->resources) {
            if (resource == handle)
                return toplevel;
        }
    }
    return NULL;
}

static uint32_t window_state(struct sh_toplevel *toplevel) {
    struct wlr_output *output = find_output(toplevel->server, toplevel->output);
    return (toplevel->sticky ? SHAODESK_WINDOW_V1_STATE_STICKY : 0) |
           (toplevel->floating || toplevel->sticky ? SHAODESK_WINDOW_V1_STATE_FLOATING : 0) |
           (toplevel->tiled ? SHAODESK_WINDOW_V1_STATE_TILED : 0) |
           (tiles_for(toplevel, output) ? SHAODESK_WINDOW_V1_STATE_TILING : 0);
}

/* Sends what changed since the object last heard, then done; everything the first time. */
static void send_window(struct sh_window_object *object) {
    struct sh_toplevel *toplevel = object->toplevel;
    if (!toplevel)
        return;
    bool changed = !object->sent;
    if (!object->sent || strcmp(object->output, toplevel->output)) {
        snprintf(object->output, sizeof(object->output), "%s", toplevel->output);
        shaodesk_window_v1_send_output(object->resource, object->output);
        changed = true;
    }
    uint32_t workspace = (uint32_t)toplevel->workspace + 1;
    if (!object->sent || object->workspace != workspace) {
        object->workspace = workspace;
        shaodesk_window_v1_send_workspace(object->resource, workspace);
        changed = true;
    }
    uint32_t state = window_state(toplevel);
    if (!object->sent || object->state != state) {
        object->state = state;
        shaodesk_window_v1_send_state(object->resource, state);
        changed = true;
    }
    // From version 3, for the taskbar to match the window with the sound its process plays.
    pid_t process = toplevel_pid(toplevel);
    uint32_t pid = process > 0 ? (uint32_t)process : 0;
    if (wl_resource_get_version(object->resource) >= SHAODESK_WINDOW_V1_PID_SINCE_VERSION &&
        (!object->sent || object->pid != pid)) {
        object->pid = pid;
        shaodesk_window_v1_send_pid(object->resource, pid);
        changed = true;
    }
    if (changed)
        shaodesk_window_v1_send_done(object->resource);
    object->sent = true;
}

static void send_windows(void *data) {
    struct sh_server *server = data;
    server->window_objects_idle = NULL;
    struct sh_window_object *object;
    wl_list_for_each(object, &server->window_objects, link) send_window(object);
}

/* Something about the windows may have changed: their objects hear of it once the change is
 * over, so that a window moving through several steps is described once, as it ends up. */
void window_objects_changed(struct sh_server *server) {
    if (!server->window_control || server->window_objects_idle ||
        wl_list_empty(&server->window_objects))
        return;
    server->window_objects_idle = wl_event_loop_add_idle(
        wl_display_get_event_loop(server->wl_display), send_windows, server);
}

/* The window is leaving the taskbar, its handles going inert: so do its objects. */
void window_objects_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server->window_control)
        return;
    struct sh_window_object *object;
    wl_list_for_each(object, &server->window_objects, link) {
        if (object->toplevel == toplevel)
            object->toplevel = NULL;
    }
}

/* The window a request is about, or NULL to ignore it: the window is gone, or the session is
 * locked. A hidden member of a window group is moved and placed with the window showing in its
 * slot. */
static struct sh_toplevel *requested(struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    struct sh_toplevel *toplevel = object ? object->toplevel : NULL;
    if (!toplevel || toplevel->server->locked)
        return NULL;
    if (toplevel->group_hidden) {
        struct sh_toplevel *member;
        wl_list_for_each(member, &toplevel->server->toplevels, link) {
            if (member->group == toplevel->group && !member->group_hidden)
                return member;
        }
    }
    return toplevel;
}

static void window_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void window_move_to_workspace(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t number) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel && number >= 1 && number <= (uint32_t)server_settings(toplevel->server)->workspaces)
        move_toplevel_to_workspace(toplevel->server, toplevel, (int)number - 1);
}

static void window_move_to_output(struct wl_client *client, struct wl_resource *resource,
                                  const char *name) {
    struct sh_toplevel *toplevel = requested(resource);
    struct wlr_output *output = toplevel ? find_output(toplevel->server, name) : NULL;
    if (output)
        move_toplevel_to_output(toplevel, output);
}

static void window_set_sticky(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel && server_settings(toplevel->server)->sticky)
        set_sticky(toplevel, true, true);
}

static void window_unset_sticky(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_sticky(toplevel, false, true);
}

static void window_set_floating(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_floating(toplevel, true, false);
}

static void window_unset_floating(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_floating(toplevel, false, false);
}

/* The capture source ext-foreign-toplevel-list's handle for the window gives, the same object;
 * an inert one when the window is gone or the session is locked, never none, which the client's
 * next request would find missing. */
static void window_get_capture_source(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t id) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    struct sh_toplevel *toplevel = object ? object->toplevel : NULL;
    wlr_ext_image_capture_source_v1_create_resource(
        toplevel ? toplevel_capture_source(toplevel) : NULL, client, id);
}

/* A capture source of its own for a small picture of the window (scaled_capture.c), inert as
 * get_capture_source's is. */
static void window_get_scaled_capture_source(struct wl_client *client,
                                             struct wl_resource *resource, uint32_t id,
                                             uint32_t width, uint32_t height) {
    if (!width || !height) {
        wl_resource_post_error(resource, SHAODESK_WINDOW_V1_ERROR_INVALID_SIZE,
                               "a picture of %ux%u pixels", width, height);
        return;
    }
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    create_scaled_capture_source(client, id, object ? object->toplevel : NULL, width, height);
}

static const struct shaodesk_window_v1_interface window_implementation = {
    .destroy = window_destroy,
    .move_to_workspace = window_move_to_workspace,
    .move_to_output = window_move_to_output,
    .set_sticky = window_set_sticky,
    .unset_sticky = window_unset_sticky,
    .set_floating = window_set_floating,
    .unset_floating = window_unset_floating,
    .get_capture_source = window_get_capture_source,
    .get_scaled_capture_source = window_get_scaled_capture_source,
};

static void window_resource_destroy(struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    wl_list_remove(&object->link);
    free(object);
}

static void control_get_window(struct wl_client *client, struct wl_resource *resource,
                               uint32_t id, struct wl_resource *handle) {
    struct sh_server *server = wl_resource_get_user_data(resource);
    struct sh_window_object *object = calloc(1, sizeof(*object));
    if (!object) {
        wl_client_post_no_memory(client);
        return;
    }
    object->resource = wl_resource_create(client, &shaodesk_window_v1_interface,
                                          wl_resource_get_version(resource), id);
    if (!object->resource) {
        free(object);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(object->resource, &window_implementation, object,
                                   window_resource_destroy);
    object->toplevel = handle_toplevel(server, handle);
    wl_list_insert(&server->window_objects, &object->link);
    send_window(object);
}

static void control_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct shaodesk_window_control_v1_interface control_implementation = {
    .destroy = control_destroy,
    .get_window = control_get_window,
};

static void control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource =
        wl_resource_create(client, &shaodesk_window_control_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &control_implementation, data, NULL);
}

/* Offered to every client, as wlr-foreign-toplevel-management is: it does nothing to a window a
 * taskbar could not already do, and shows nothing of one that ext-foreign-toplevel-list's
 * capture sources do not, but for which process made it. */
void window_control_init(struct sh_server *server) {
    wl_list_init(&server->window_objects);
    server->window_control = wl_global_create(server->wl_display,
                                              &shaodesk_window_control_v1_interface, 3, server,
                                              control_bind);
    if (!server->window_control)
        wlr_log(WLR_ERROR, "Cannot offer shaodesk-window-control-v1");
}
