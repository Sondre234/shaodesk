/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Touchpad gestures: the swipes, pinches and holds of the cursor's devices, passed on to the
 * surface the pointer is on through pointer-gestures-unstable-v1, for browsers' pinch-zoom and
 * swipes back and forward and GTK's gestures. */
#include "server.h"

static void swipe_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void swipe_update(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures, server->seat,
                                              event->time_msec, event->dx, event->dy);
}

static void swipe_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_swipe_end(server->pointer_gestures, server->seat,
                                           event->time_msec, event->cancelled);
}

static void pinch_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_begin(server->pointer_gestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void pinch_update(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_update(server->pointer_gestures, server->seat,
                                              event->time_msec, event->dx, event->dy,
                                              event->scale, event->rotation);
}

static void pinch_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_end(server->pointer_gestures, server->seat,
                                           event->time_msec, event->cancelled);
}

/* Fingers resting on the touchpad: GTK and Firefox stop kinetic scrolling on a hold. */
static void hold_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_hold_begin(server->pointer_gestures, server->seat,
                                            event->time_msec, event->fingers);
}

static void hold_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, hold_end);
    struct wlr_pointer_hold_end_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_hold_end(server->pointer_gestures, server->seat,
                                          event->time_msec, event->cancelled);
}

/* Offers pointer-gestures-unstable-v1 and listens to the cursor's gestures. wlroots sends each
 * one to the client of the surface with pointer focus, if it bound the gestures of that seat's
 * pointer, and keeps sending a gesture's updates and end there once it began. */
void gestures_init(struct sh_server *server) {
    server->pointer_gestures = wlr_pointer_gestures_v1_create(server->wl_display);
    struct wlr_cursor *cursor = server->cursor;
    add_listener(&cursor->events.swipe_begin, &server->swipe_begin, swipe_begin);
    add_listener(&cursor->events.swipe_update, &server->swipe_update, swipe_update);
    add_listener(&cursor->events.swipe_end, &server->swipe_end, swipe_end);
    add_listener(&cursor->events.pinch_begin, &server->pinch_begin, pinch_begin);
    add_listener(&cursor->events.pinch_update, &server->pinch_update, pinch_update);
    add_listener(&cursor->events.pinch_end, &server->pinch_end, pinch_end);
    add_listener(&cursor->events.hold_begin, &server->hold_begin, hold_begin);
    add_listener(&cursor->events.hold_end, &server->hold_end, hold_end);
}

void gestures_finish(struct sh_server *server) {
    wl_list_remove(&server->swipe_begin.link);
    wl_list_remove(&server->swipe_update.link);
    wl_list_remove(&server->swipe_end.link);
    wl_list_remove(&server->pinch_begin.link);
    wl_list_remove(&server->pinch_update.link);
    wl_list_remove(&server->pinch_end.link);
    wl_list_remove(&server->hold_begin.link);
    wl_list_remove(&server->hold_end.link);
}
