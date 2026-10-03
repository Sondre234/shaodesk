/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
#include "server.h"

/* BEGIN FORWARD */
static bool handle_keybinding(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                              xkb_keysym_t sym);
/* END FORWARD */

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
    struct sh_server *server = keyboard->server;
    uint32_t held = server->switcher.modifiers;
    if (server->switcher.open && held &&
        (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & held) != held)
        switcher_close(server, server->switcher.selected);
}

static int keyboard_repeat(void *data) {
    struct sh_keyboard *keyboard = data;
    struct sh_server *server = keyboard->server;
    if (server->locked)
        return 0;
    run_action(server, keyboard->repeat_action, keyboard->repeat_argument);
    int rate = keyboard->wlr_keyboard->repeat_info.rate;
    wl_event_source_timer_update(keyboard->repeat_timer, rate > 0 ? 1000 / rate : 0);
    return 0;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct sh_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat;

    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, seat);
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    // Any key pressed or the repeating one released stops the repeat.
    if (keyboard->repeat_timer && (event->state == WL_KEYBOARD_KEY_STATE_PRESSED ||
                                   event->keycode == keyboard->repeat_keycode))
        wl_event_source_timer_update(keyboard->repeat_timer, 0);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms && !handled; ++i)
            handled = handle_keybinding(keyboard, event->keycode, modifiers, syms[i]);
        if (!handled) {
            xkb_layout_index_t layout =
                xkb_state_key_get_layout(keyboard->wlr_keyboard->xkb_state, keycode);
            const xkb_keysym_t *raw;
            int nraw = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, keycode,
                                                        layout, 0, &raw);
            for (int i = 0; i < nraw && !handled; ++i)
                handled = handle_keybinding(keyboard, event->keycode, modifiers, raw[i]);
        }
        if (event->keycode <= KEY_MAX)
            keyboard->consumed[event->keycode] = handled;
    } else if (event->keycode <= KEY_MAX) {
        handled = keyboard->consumed[event->keycode];
        keyboard->consumed[event->keycode] = false;
        if (server->peeking && server->peek_keycode == event->keycode && handled)
            set_peek(server, false);
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    // The keyboard holding the switcher open goes away as if its modifiers were released.
    struct sh_server *server = keyboard->server;
    if (server->switcher.open && server->switcher.modifiers &&
        (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & server->switcher.modifiers))
        switcher_close(server, server->switcher.selected);
    if (server->peek_keyboard == keyboard)
        set_peek(server, false);
    if (keyboard->repeat_timer)
        wl_event_source_remove(keyboard->repeat_timer);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

bool configure_keyboard(struct sh_server *server, struct wlr_keyboard *keyboard) {
    const struct sh_settings *settings = server_settings(server);
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return false;
    struct xkb_rule_names names = {.layout = settings->keyboard_layout,
                                   .variant = settings->keyboard_variant,
                                   .model = settings->keyboard_model,
                                   .options = settings->keyboard_options};
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(context);
    if (!keymap)
        return false;
    bool ok = wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    wlr_keyboard_set_repeat_info(keyboard, settings->repeat_rate, settings->repeat_delay);
    return ok;
}

static void server_new_keyboard(struct sh_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct sh_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    // A virtual keyboard (wtype and the like) sends its own keymap, which ours would replace.
    bool is_virtual = wlr_input_device_get_virtual_keyboard(device) != NULL;
    if (!is_virtual && !configure_keyboard(server, wlr_keyboard)) {
        wlr_log(WLR_ERROR, "Failed to configure keyboard");
        free(keyboard);
        return;
    }
    if (is_virtual) { // held keys repeat bindings as on a real keyboard
        const struct sh_settings *settings = server_settings(server);
        wlr_keyboard_set_repeat_info(wlr_keyboard, settings->repeat_rate, settings->repeat_delay);
    }
    keyboard->repeat_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                     keyboard_repeat, keyboard);

    add_listener(&wlr_keyboard->events.modifiers, &keyboard->modifiers, keyboard_handle_modifiers);
    add_listener(&wlr_keyboard->events.key, &keyboard->key, keyboard_handle_key);
    add_listener(&device->events.destroy, &keyboard->destroy, keyboard_handle_destroy);

    // It becomes the seat keyboard on its first key, once its keymap has arrived.
    if (!is_virtual)
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    wl_list_insert(&server->keyboards, &keyboard->link);
}

/* mouse.* applies to every pointer, touchpad.* to devices that can tap. Unset settings keep
 * the device's defaults. Only libinput devices (standalone sessions) have any of these. */
void configure_pointer(struct sh_server *server, struct wlr_input_device *device) {
#if WLR_HAS_LIBINPUT_BACKEND
    if (!wlr_input_device_is_libinput(device))
        return;
    struct libinput_device *handle = wlr_libinput_get_device_handle(device);
    const struct sh_settings *settings = server_settings(server);
    bool touchpad = libinput_device_config_tap_get_finger_count(handle) > 0;
    if (libinput_device_config_accel_is_available(handle)) {
        if (settings->pointer_speed_set)
            libinput_device_config_accel_set_speed(handle, settings->pointer_speed);
        if (settings->pointer_accel >= 0)
            libinput_device_config_accel_set_profile(
                handle, settings->pointer_accel ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                                                : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
    }
    int natural = touchpad && settings->touchpad_natural_scroll >= 0
                      ? settings->touchpad_natural_scroll
                      : settings->mouse_natural_scroll;
    if (natural >= 0 && libinput_device_config_scroll_has_natural_scroll(handle))
        libinput_device_config_scroll_set_natural_scroll_enabled(handle, natural);
    if (touchpad && settings->touchpad_tap >= 0)
        libinput_device_config_tap_set_enabled(handle, settings->touchpad_tap
                                                           ? LIBINPUT_CONFIG_TAP_ENABLED
                                                           : LIBINPUT_CONFIG_TAP_DISABLED);
    if (settings->touchpad_dwt >= 0 && libinput_device_config_dwt_is_available(handle))
        libinput_device_config_dwt_set_enabled(handle, settings->touchpad_dwt
                                                           ? LIBINPUT_CONFIG_DWT_ENABLED
                                                           : LIBINPUT_CONFIG_DWT_DISABLED);
#else
    (void)server;
    (void)device;
#endif
}

static void pointer_destroy(struct wl_listener *listener, void *data) {
    struct sh_pointer *pointer = wl_container_of(listener, pointer, destroy);
    wl_list_remove(&pointer->destroy.link);
    wl_list_remove(&pointer->link);
    free(pointer);
}

static void server_new_pointer(struct sh_server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
    struct sh_pointer *pointer = calloc(1, sizeof(*pointer));
    if (!pointer)
        return;
    pointer->server = server;
    pointer->device = device;
    add_listener(&device->events.destroy, &pointer->destroy, pointer_destroy);
    wl_list_insert(&server->pointers, &pointer->link);
    configure_pointer(server, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }
}

/* Virtual input lets tools such as wtype and wlrctl drive the session, e.g. in tests. */
void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    server_new_input(&server->new_input, &keyboard->keyboard.base);
}

void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    struct wlr_input_device *device = &event->new_pointer->pointer.base;
    server_new_input(&server->new_input, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(server->cursor, device, event->suggested_output);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

/* Drag-and-drop (browser tabs, files into chat windows): only from a real button press. */
void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;
    if (!server->locked && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

void seat_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, start_drag);
    struct wlr_drag *drag = data;
    wlr_scene_node_set_position(&server->drag_icons->node, server->cursor->x, server->cursor->y);
    // The scene helper removes the icon's node when the icon goes away.
    if (drag->icon)
        wlr_scene_drag_icon_create(server->drag_icons, drag->icon);
}

/* Pointer constraints (games, remote desktops, pointer lock in browsers) apply to the
 * keyboard-focused surface only, and only while the pointer is over it. */
static void set_active_constraint(struct sh_server *server,
                                  struct wlr_pointer_constraint_v1 *constraint) {
    if (server->active_constraint == constraint)
        return;
    if (server->active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
    server->active_constraint = constraint;
    if (constraint)
        wlr_pointer_constraint_v1_send_activated(constraint);
}

static void constraint_destroy(struct wl_listener *listener, void *data) {
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct sh_server *server = constraint->data;
    wl_list_remove(&listener->link);
    free(listener);
    if (server->active_constraint == constraint)
        server->active_constraint = NULL;
}

void server_new_constraint(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct wl_listener *destroy = calloc(1, sizeof(*destroy));
    if (!destroy)
        return;
    constraint->data = server;
    add_listener(&constraint->events.destroy, destroy, constraint_destroy);
    if (constraint->surface == server->seat->keyboard_state.focused_surface)
        set_active_constraint(server, constraint);
}

void seat_keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, keyboard_focus_change);
    struct wlr_seat_keyboard_focus_change_event *event = data;
    set_active_constraint(server, event->new_surface
                                      ? wlr_pointer_constraints_v1_constraint_for_surface(
                                            server->constraints, event->new_surface, server->seat)
                                      : NULL);
}

#if WLR_HAS_SESSION
// Returns the VT a key switches to, or 0. Ctrl+AltGr+Fn counts as Ctrl+Alt+Fn: some keyboards'
// only Alt key is Right Alt, which AltGr layouts turn into Level3 instead of Alt.
static unsigned vt_for_key(uint32_t modifiers, xkb_keysym_t sym) {
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12)
        return sym - XKB_KEY_XF86Switch_VT_1 + 1;
    if ((modifiers & WLR_MODIFIER_CTRL) && (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_MOD5)) &&
        sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return sym - XKB_KEY_F1 + 1;
    return 0;
}
#endif

static bool handle_keybinding(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                              xkb_keysym_t sym) {
    struct sh_server *server = keyboard->server;
#if WLR_HAS_SESSION
    unsigned vt = vt_for_key(modifiers, sym);
    if (server->session && vt) {
        wlr_session_change_vt(server->session, vt);
        return true;
    }
#endif
    if (server->locked)
        return false; // Every other key belongs to the lock screen.
    if (server->switcher.open) {
        switcher_key(server, modifiers, sym);
        return true;
    }
    if (server->overview.open) {
        // The overview's own bindings (toggling it again) work as bound; other keys are its.
        int bound_argument = 0;
        enum sh_action bound =
            modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)
                ? server->callbacks->key(server->callbacks->userdata, modifiers, sym,
                                         &bound_argument)
                : SH_NONE;
        if (bound == SH_OVERVIEW_TOGGLE || bound == SH_OVERVIEW_CONFIRM ||
            bound == SH_OVERVIEW_CANCEL)
            run_action(server, bound, bound_argument);
        else
            overview_key(server, modifiers, sym);
        return true;
    }
    int argument = 0;
    enum sh_action action =
        server->callbacks->key(server->callbacks->userdata, modifiers, sym, &argument);
    if (action == SH_NONE)
        return false;
    if (action == SH_PEEK) {
        // Held: the desktop shows until the key comes back up.
        server->peek_keycode = keycode;
        server->peek_keyboard = keyboard;
        set_peek(server, true);
        server->peek_keycode = keycode; // set_peek only forgets it when peeking ends
        return true;
    }
    if (action == SH_SWITCHER_NEXT || action == SH_SWITCHER_PREV) {
        // Held, the binding's modifiers keep it open. Shift may come and go to step backward.
        uint32_t held =
            WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | WLR_MODIFIER_MOD5;
        switcher_open(server, action == SH_SWITCHER_PREV, modifiers & held,
                      xkb_keysym_to_lower(sym));
        return true;
    }
    run_action(server, action, argument);
    int rate = keyboard->wlr_keyboard->repeat_info.rate;
    if (action >= SH_RESIZE_LEFT && action <= SH_RESIZE_DOWN && rate > 0 &&
        keyboard->repeat_timer) {
        keyboard->repeat_keycode = keycode;
        keyboard->repeat_action = action;
        keyboard->repeat_argument = argument;
        wl_event_source_timer_update(keyboard->repeat_timer,
                                     keyboard->wlr_keyboard->repeat_info.delay);
    }
    return true;
}
