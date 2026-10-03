/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Derived from wlroots TinyWL 0.20.2; see vendor/tinywl/LICENSE. */
#include "server.h"

_Static_assert((unsigned)SH_ALT == (unsigned)WLR_MODIFIER_ALT &&
                   (unsigned)SH_SHIFT == (unsigned)WLR_MODIFIER_SHIFT &&
                   (unsigned)SH_CTRL == (unsigned)WLR_MODIFIER_CTRL &&
                   (unsigned)SH_LOGO == (unsigned)WLR_MODIFIER_LOGO,
               "C++ configuration and wlroots modifier bits must agree");
_Static_assert((unsigned)SH_EDGE_TOP == (unsigned)WLR_EDGE_TOP &&
                   (unsigned)SH_EDGE_BOTTOM == (unsigned)WLR_EDGE_BOTTOM &&
                   (unsigned)SH_EDGE_LEFT == (unsigned)WLR_EDGE_LEFT &&
                   (unsigned)SH_EDGE_RIGHT == (unsigned)WLR_EDGE_RIGHT,
               "tiling and wlroots edge bits must agree");

uint64_t now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}


void add_listener(struct wl_signal *signal, struct wl_listener *listener,
                  wl_notify_func_t notify) {
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

const struct sh_settings *server_settings(struct sh_server *server) {
    return server->callbacks->settings(server->callbacks->userdata);
}

void deactivate_toplevel(struct sh_server *server) {
    if (!server->focused_toplevel)
        return;
    struct sh_toplevel *old = server->focused_toplevel;
    // Fullscreen the client asked for stays over the panels, so a video keeps covering its
    // output while another output has focus; raising a window over it lowers it.
    if (old->fullscreen && !old->fullscreen_cover)
        wlr_scene_node_reparent(&old->scene_tree->node, server->windows);
    toplevel_set_activated(old, false);
    if (old->foreign)
        wlr_foreign_toplevel_handle_v1_set_activated(old->foreign, false);
    server->focused_toplevel = NULL;
    refresh_frame(old);
}

/* Puts the fullscreen windows covering the panels on `toplevel`'s output down among the
 * others, so that it can come to the front over them. */
static void lower_fullscreen_covers(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = toplevel_output(toplevel);
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (other != toplevel && other->fullscreen && other->fullscreen_cover &&
            other->scene_tree && toplevel_output(other) == output)
            wlr_scene_node_reparent(&other->scene_tree->node, server->windows);
    }
}

/* Gives the window keyboard focus; `raise` also brings it to the front. */
/* Gives the surface keyboard focus even while the seat has no keyboard (headless, or before a
 * virtual keyboard connects), so the first keyboard to appear types into it. */
void keyboard_enter(struct wlr_seat *seat, struct wlr_surface *surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard)
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
}

void focus_toplevel_raise(struct sh_toplevel *toplevel, bool raise) {
    if (!toplevel || toplevel->server->locked)
        return;
    struct sh_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    if (toplevel->swallowed && toplevel->swallow_peer)
        swallow_release(toplevel->swallow_peer); // asked for by hand: it comes back
    if (toplevel->group_hidden)
        group_show(toplevel); // another tab of its group: it takes the group's slot
    // A hidden scratchpad window activated from the taskbar comes to the focused output, as
    // scratchpad_show brings it.
    if (toplevel->scratchpad && toplevel->minimized)
        center_scratchpad(toplevel, focused_output(server));
    if (toplevel->output[0] && !toplevel->sticky &&
        toplevel->workspace != *output_workspace(server, toplevel->output)) {
        if (server->grabbed_toplevel)
            reset_cursor_mode(server);
        show_workspace(server, toplevel->output, toplevel->workspace);
    }
    deactivate_toplevel(server);
    server->focused_layer = NULL;
    server->focused_toplevel = toplevel;
    bool was_urgent = toplevel->urgent;
    toplevel->urgent = false; // it has the user's attention now
    bool was_minimized = toplevel->minimized;
    toplevel->minimized = false;
    if (was_minimized && wants_tiling(toplevel, NULL))
        tile_toplevel(toplevel, NULL, NULL, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
    // Panels stay reachable once a fullscreen window loses focus.
    if (raise && !toplevel->fullscreen)
        lower_fullscreen_covers(toplevel);
    if (raise || toplevel->fullscreen) {
        wlr_scene_node_reparent(&toplevel->scene_tree->node,
                                toplevel->fullscreen ? fullscreen_tree(toplevel) : server->windows);
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    }
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    toplevel_set_activated(toplevel, true);
    refresh_frame(toplevel);
    if (toplevel->foreign) {
        wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, false);
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign, true);
    }
    if (toplevel_accepts_keyboard(toplevel))
        keyboard_enter(seat, toplevel_surface(toplevel));
    set_active_output(server, toplevel->output);
    // The scrolling view follows focus.
    if (toplevel->tiled && sh_tiling_set_focus(server->tiling, toplevel)) {
        struct wlr_output *output = tiled_output(toplevel);
        if (output)
            reflow_output(server, output);
    }
    if (was_urgent) {
        wlr_log(WLR_INFO, "Urgent window focused");
        notify_subscribers(server);
    }
}

void focus_toplevel(struct sh_toplevel *toplevel) { focus_toplevel_raise(toplevel, true); }

/* Urgent windows. An unfocused window that asks for attention (xdg-activation, an X11 urgency
 * hint) is marked urgent under windows.activation = "urgent": its border pulses for a few
 * seconds and stays in the urgent color, the taskbar and workspace indicator show it, and
 * focus_urgent goes to the one that asked first. Focusing it, or unmapping it, ends it. */
enum { URGENT_PULSE_MS = 4000, URGENT_PULSE_PERIOD_MS = 1200, URGENT_TICK_MS = 40 };

/* How bright an urgent window's border is now: it starts at full strength and pulses down and
 * up for URGENT_PULSE_MS, then holds. Without animations it holds at once. */
float urgent_pulse(struct sh_toplevel *toplevel, int64_t now) {
    if (!toplevel->urgent || !server_settings(toplevel->server)->animations)
        return 1;
    int64_t elapsed = now - toplevel->urgent_since;
    if (elapsed < 0 || elapsed >= URGENT_PULSE_MS)
        return 1;
    return 0.65F + 0.35F * cosf((float)(2 * M_PI * (double)elapsed / URGENT_PULSE_PERIOD_MS));
}

static bool urgent_pulsing(struct sh_toplevel *toplevel, int64_t now) {
    return toplevel->urgent && now - toplevel->urgent_since < URGENT_PULSE_MS + URGENT_TICK_MS;
}

/* Redraws the borders of the windows that pulse while any does; the tick after the last one
 * finishes draws it at rest. */
static int urgent_tick(void *data) {
    struct sh_server *server = data;
    int64_t now = now_ms();
    bool more = false;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!urgent_pulsing(toplevel, now))
            continue;
        refresh_frame(toplevel);
        more = true;
    }
    if (more && server->urgent_timer)
        wl_event_source_timer_update(server->urgent_timer, URGENT_TICK_MS);
    return 0;
}

static bool toplevel_can_be_urgent(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return false;
#endif
    return toplevel_mapped(toplevel);
}

/* Marks or unmarks `toplevel` as urgent. The focused window never is: it has the attention. */
void set_urgent(struct sh_toplevel *toplevel, bool urgent) {
    struct sh_server *server = toplevel->server;
    if (toplevel->urgent == urgent)
        return;
    if (urgent && (!toplevel_can_be_urgent(toplevel) || server->focused_toplevel == toplevel))
        return;
    toplevel->urgent = urgent;
    if (urgent) {
        toplevel->urgent_order = ++server->urgent_serial;
        toplevel->urgent_since = now_ms();
        wlr_log(WLR_INFO, "Window %s is urgent", toplevel_app_id(toplevel) ? toplevel_app_id(toplevel) : "");
        if (server->urgent_timer)
            wl_event_source_timer_update(server->urgent_timer, URGENT_TICK_MS);
    }
    refresh_frame(toplevel);
    notify_subscribers(server);
}

/* A client asks to be focused (xdg-activation, _NET_ACTIVE_WINDOW): what it gets follows
 * windows.activation. */
void activation_requested(struct sh_toplevel *toplevel) {
    if (!toplevel_can_be_urgent(toplevel))
        return;
    switch (server_settings(toplevel->server)->activation) {
    case SH_ACTIVATION_FOCUS:
        focus_toplevel(toplevel);
        break;
    case SH_ACTIVATION_URGENT:
        set_urgent(toplevel, true);
        break;
    default:
        break;
    }
}

/* The window that has been urgent the longest, or NULL. */
static struct sh_toplevel *oldest_urgent(struct sh_server *server) {
    struct sh_toplevel *toplevel, *oldest = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->urgent && toplevel_can_be_urgent(toplevel) &&
            (!oldest || toplevel->urgent_order < oldest->urgent_order))
            oldest = toplevel;
    }
    return oldest;
}

void focus_urgent(struct sh_server *server) {
    if (server->locked)
        return;
    struct sh_toplevel *toplevel = oldest_urgent(server);
    if (!toplevel)
        return;
    focus_toplevel(toplevel);
    pointer_follow(toplevel);
}

/* Whether hovering `toplevel` may focus it: not during a drag, a popup or menu grab, or while
 * a panel or launcher holds the keyboard. */
bool hover_focuses(struct sh_server *server, struct sh_toplevel *toplevel) {
    struct wlr_seat *seat = server->seat;
    if (!server_settings(server)->focus_follows_mouse || server->locked ||
        toplevel == server->focused_toplevel || server->focused_layer ||
        !toplevel_visible(toplevel) || seat->pointer_state.button_count > 0 ||
        wlr_seat_pointer_has_grab(seat) || wlr_seat_keyboard_has_grab(seat))
        return false;
#if WLR_HAS_XWAYLAND
    // X11 menus are override-redirect windows that grab inside the X server, unseen here.
    if (toplevel->unmanaged || !wl_list_empty(&server->unmanaged->children))
        return false;
#endif
    return true;
}

void focus_previous(struct sh_server *server) {
    if (server->locked)
        return;
    server->focused_layer = NULL;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel)) {
            focus_toplevel(toplevel);
            return;
        }
    }
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
}

/* Focuses the window focused before the current one, wherever it is (its output switches to
 * its workspace). Windows hidden in the scratchpad or minimized are not in the history. */
void focus_last(struct sh_server *server) {
    if (server->locked)
        return;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (toplevel == server->focused_toplevel || toplevel->minimized || toplevel->swallowed ||
            !toplevel_mapped(toplevel))
            continue;
        focus_toplevel(toplevel);
        pointer_follow(toplevel);
        return;
    }
}

/* Focuses the topmost visible window on `output`, else nothing. */
void focus_top_on(struct sh_server *server, struct wlr_output *output) {
    server->focused_layer = NULL;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel) && find_output(server, toplevel->output) == output) {
            focus_toplevel(toplevel);
            return;
        }
    }
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    notify_subscribers(server);
}

/* The bare desktop of an output counts as a window there: pointing at it (with focus following
 * the mouse) or clicking it on another output than the focused window's makes that output the
 * focused one and takes the keyboard from the window, as sway focuses an empty workspace. */
void focus_desktop(struct sh_server *server, struct wlr_output *output) {
    if (!output || server->locked || server->focused_layer)
        return;
    struct sh_toplevel *focused = server->focused_toplevel;
    if (focused ? find_output(server, focused->output) == output
                : !strcmp(server->active_output, output->name))
        return;
    set_active_output(server, output->name);
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    notify_subscribers(server);
}

void focus_layer(struct sh_layer *layer) {
    if (layer->server->locked || layer->surface->current.keyboard_interactive ==
                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        return;
    struct sh_server *server = layer->server;
    deactivate_toplevel(server);
    server->focused_layer = layer;
    keyboard_enter(server->seat, layer->surface->surface);
}

/* The window keyboard actions apply to: the focused one, else the topmost visible. */
struct sh_toplevel *current_toplevel(struct sh_server *server) {
    if (server->focused_toplevel)
        return server->focused_toplevel;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel))
            return toplevel;
    }
    return NULL;
}

/* The nearest visible window from `from` in a direction (`sign` -1 is left or up): first those
 * level with it (overlapping across the direction), then by distance between centres.
 * `tiles_only` looks only at tiles sharing its tiling. */
struct sh_toplevel *toplevel_toward(struct sh_toplevel *from_toplevel, bool horizontal,
                                    int sign, bool tiles_only) {
    struct sh_server *server = from_toplevel->server;
    struct wlr_output *tiling = tiles_only ? tiled_output(from_toplevel) : NULL;
    struct wlr_box from = toplevel_box(from_toplevel);
    double from_x = from.x + from.width / 2.0, from_y = from.y + from.height / 2.0;
    struct sh_toplevel *best = NULL, *toplevel;
    bool best_level = false;
    double best_distance = 0;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel == from_toplevel || !toplevel_visible(toplevel))
            continue;
        if (tiles_only && (!toplevel->tiled || toplevel->fullscreen ||
                           tiled_output(toplevel) != tiling))
            continue;
        struct wlr_box box = toplevel_box(toplevel);
        double x = box.x + box.width / 2.0, y = box.y + box.height / 2.0;
        double along = horizontal ? x - from_x : y - from_y;
        if (along * sign <= 0)
            continue;
        bool level = horizontal ? box.y < from.y + from.height && from.y < box.y + box.height
                                : box.x < from.x + from.width && from.x < box.x + box.width;
        double distance = hypot(x - from_x, y - from_y);
        if (!best || (level && !best_level) || (level == best_level && distance < best_distance)) {
            best = toplevel;
            best_level = level;
            best_distance = distance;
        }
    }
    return best;
}

/* Focus follows the mouse on its next move, so keyboard actions take the pointer along to the
 * window they focus or move: just inside its bottom-right corner, out of the way of what is
 * being read or typed. Windows still opening or gliding are not yet drawn where they are, so
 * they land first; otherwise the pointer could hover, and focus, whichever window is passing
 * its destination. */
void pointer_follow(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server_settings(server)->focus_follows_mouse)
        return;
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) sh_anim_finish(&other->anim);
    // The corner of the smaller of its drawn and requested sizes, so the pointer is inside the
    // window both before and after a pending resize.
    struct wlr_box box = toplevel_box(toplevel), drawn = toplevel_geometry(toplevel);
    int width = drawn.width > 0 && drawn.width < box.width ? drawn.width : box.width;
    int height = drawn.height > 0 && drawn.height < box.height ? drawn.height : box.height;
    const int inset = 8; // clear of client-side resize edges
    double x = box.x + (width > 2 * inset ? width - inset : width / 2.0);
    double y = box.y + (height > 2 * inset ? height - inset : height / 2.0);
    wlr_cursor_warp(server->cursor, NULL, x, y);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server, now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

/* Focus moves to the nearest window that way, from the focused window or else the topmost on
 * the focused output. With none there, it moves to the next output that way, as sway does, so
 * an empty output can be reached from the keyboard: its topmost window, else its bare desktop
 * with the pointer at its centre, where new windows open. */
void focus_direction(struct sh_server *server, enum sh_action action) {
    if (server->locked)
        return;
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *current = server->focused_toplevel, *toplevel;
    if (!current) {
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel_visible(toplevel) && find_output(server, toplevel->output) == output) {
                current = toplevel;
                break;
            }
        }
    }
    bool horizontal = action == SH_FOCUS_LEFT || action == SH_FOCUS_RIGHT;
    int sign = action == SH_FOCUS_LEFT || action == SH_FOCUS_UP ? -1 : 1;
    struct sh_toplevel *best;
    if (current && current->tiled && toplevel_layout(current) == SH_LAYOUT_SCROLL)
        // Columns off the output count too: step through the strip.
        best = sh_tiling_scroll_step(server->tiling, current, horizontal ? sign : 0,
                                     horizontal ? 0 : sign);
    else
        best = current ? toplevel_toward(current, horizontal, sign, false) : NULL;
    if (current && current->tiled && toplevel_layout(current) == SH_LAYOUT_MONOCLE) {
        // Every tile covers the same area, so the arrows step through them.
        best = sh_tiling_neighbour(server->tiling, current, sign);
        if (best) {
            focus_toplevel(best);
            pointer_follow(best);
        }
        return;
    }
    if (best) {
        focus_toplevel(best);
        pointer_follow(best);
        return;
    }
    if (!output)
        return;
    static const enum wlr_direction directions[] = {WLR_DIRECTION_LEFT, WLR_DIRECTION_RIGHT,
                                                    WLR_DIRECTION_UP, WLR_DIRECTION_DOWN};
    struct wlr_box from;
    if (current)
        from = toplevel_box(current);
    else
        wlr_output_layout_get_box(server->output_layout, output, &from);
    struct wlr_output *next = wlr_output_layout_adjacent_output(
        server->output_layout, directions[action - SH_FOCUS_LEFT], output,
        from.x + from.width / 2.0, from.y + from.height / 2.0);
    if (!next)
        return;
    set_active_output(server, next->name);
    focus_top_on(server, next);
    if (server->focused_toplevel) {
        pointer_follow(server->focused_toplevel);
        return;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, next, &box);
    wlr_cursor_warp(server->cursor, NULL, box.x + box.width / 2.0, box.y + box.height / 2.0);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server, now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static void configure_animations(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_animator_config config = {.enabled = settings->animations,
                                        .speed = settings->animation_speed,
                                        .late_ms = settings->animation_late_ms};
    memcpy(config.styles, settings->animation_styles, sizeof(config.styles));
    sh_animator_configure(server->animator, &config);
}

void reload_config(struct sh_server *server) {
    if (!server->callbacks->reload(server->callbacks->userdata))
        return;
    struct sh_output *overridden;
    wl_list_for_each(overridden, &server->outputs, link) overridden->has_override = false;
    wl_list_for_each(overridden, &server->disabled_outputs, link) overridden->has_override = false;
    ++server->config_generation;
    configure_animations(server);
    night_light_update(server);
    if (!server_settings(server)->overview)
        overview_dismiss(server);
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (wlr_input_device_get_virtual_keyboard(&keyboard->wlr_keyboard->base))
            continue;
        if (!configure_keyboard(server, keyboard->wlr_keyboard))
            wlr_log(WLR_ERROR, "Could not apply reloaded keymap");
    }
    struct sh_pointer *pointer;
    wl_list_for_each(pointer, &server->pointers, link) configure_pointer(server, pointer->device);
    // Enable outputs before disabling others, so a swap never leaves none on.
    struct sh_output *output, *temporary;
    wl_list_for_each_safe(output, temporary, &server->disabled_outputs, link)
        configure_output(server, output);
    wl_list_for_each_safe(output, temporary, &server->outputs, link)
        configure_output(server, output);
    arrange_outputs(server);
    reconfigure_tiling(server);
    return_home_windows(server);
    struct sh_toplevel *toplevel;
    // With features.sticky off, sticky windows stay on their output's current workspace.
    if (!server_settings(server)->sticky) {
        wl_list_for_each(toplevel, &server->toplevels, link) set_sticky(toplevel, false, true);
    }
    rehome_tiles(server);
    int count = server_settings(server)->workspaces;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->workspace >= count)
            set_toplevel_workspace(toplevel, count - 1);
    }
    for (size_t i = 0; i < sizeof(server->output_workspaces) / sizeof(server->output_workspaces[0]);
         ++i) {
        if (server->output_workspaces[i].current >= count)
            server->output_workspaces[i].current = count - 1;
        if (server->output_workspaces[i].previous >= count ||
            server->output_workspaces[i].previous == server->output_workspaces[i].current)
            server->output_workspaces[i].previous = -1;
    }
    if (!server_settings(server)->scratchpad)
        empty_scratchpad(server);
    if (!server_settings(server)->groups)
        dissolve_groups(server);
    show_workspaces(server);
    if (server->focused_toplevel && !toplevel_visible(server->focused_toplevel)) {
        deactivate_toplevel(server);
        focus_previous(server);
    }
    // Gaps, borders, and opacity may have changed.
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
}

static int terminate_signal(int signal_number, void *data) {
    struct sh_server *server = data;
    wl_display_terminate(server->wl_display);
    return 0;
}
static int reload_signal(int signal_number, void *data) {
    reload_config(data);
    return 0;
}
#define CONFIG_SETTLE_MS 150
static int config_settled(void *data) {
    reload_config(data);
    return 0;
}
static int config_watch_ready(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    if (server->callbacks->config_changed(server->callbacks->userdata) && server->config_timer)
        wl_event_source_timer_update(server->config_timer, CONFIG_SETTLE_MS);
    return 0;
}
static int reap_children(int signal_number, void *data) {
    struct sh_server *server = data;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        if (server->callbacks->child_exited)
            server->callbacks->child_exited(server->callbacks->userdata, pid);
    }
    return 0;
}

int sh_run(const struct sh_callbacks *callbacks, enum sh_backend_mode mode) {
    wlr_log_init(WLR_INFO, NULL);
    if (mode == SH_BACKEND_SESSION &&
        !(WLR_HAS_SESSION && WLR_HAS_DRM_BACKEND && WLR_HAS_LIBINPUT_BACKEND)) {
        wlr_log(WLR_ERROR, "wlroots needs session, DRM, and libinput support for --session");
        return 1;
    }
    const char *backends = mode == SH_BACKEND_SESSION    ? "drm,libinput"
                           : mode == SH_BACKEND_HEADLESS ? "headless"
                                                         : "wayland";
    if (setenv("WLR_BACKENDS", backends, 1) < 0)
        return 1;
    if (mode == SH_BACKEND_HEADLESS)
        setenv("WLR_HEADLESS_OUTPUTS", "1", 0); // tests may ask for more

    struct sh_server server = {.callbacks = callbacks, .config_generation = 1};
    wl_list_init(&server.subscribers);
    server.tiling = sh_tiling_create();
    if (!server.tiling)
        return 1;

    server.wl_display = wl_display_create();
    if (!server.wl_display)
        return 1;
    struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
    struct wl_event_source *sigint =
        wl_event_loop_add_signal(loop, SIGINT, terminate_signal, &server);
    struct wl_event_source *sigterm =
        wl_event_loop_add_signal(loop, SIGTERM, terminate_signal, &server);
    struct wl_event_source *sighup = wl_event_loop_add_signal(loop, SIGHUP, reload_signal, &server);
    struct wl_event_source *sigchld =
        wl_event_loop_add_signal(loop, SIGCHLD, reap_children, &server);
    server.animator = sh_animator_create(loop);
    if (!server.animator)
        return 1;
    configure_animations(&server);
    configure_layouts(&server);
    server.night_kelvin = SH_KELVIN_NEUTRAL;
    sh_fade_init(&server.zoom_fade, 1);
    server.zoom_target = 1;
    server.night_clock = -1;
    if (getenv("SHAODESK_NIGHT_LIGHT_TIME")) {
        // A fixed clock, so tests can run at any hour.
        double minutes = 0;
        if (sh_parse_clock(getenv("SHAODESK_NIGHT_LIGHT_TIME"), &minutes))
            server.night_clock = minutes;
    }
    server.night_timer = wl_event_loop_add_timer(loop, night_light_tick, &server);
    server.corner_timer = wl_event_loop_add_timer(loop, hot_corner_tick, &server);
    server.urgent_timer = wl_event_loop_add_timer(loop, urgent_tick, &server);
    int config_fd = callbacks->config_watch(callbacks->userdata);
    if (config_fd >= 0) {
        server.config_timer = wl_event_loop_add_timer(loop, config_settled, &server);
        server.config_watch = wl_event_loop_add_fd(loop, config_fd, WL_EVENT_READABLE,
                                                   config_watch_ready, &server);
    }
    sh_corner_dwell_init(&server.corner_dwell);

    server.backend = wlr_backend_autocreate(loop,
#if WLR_HAS_SESSION
                                            &server.session
#else
                                            NULL
#endif
    );
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }
#if WLR_HAS_SESSION
    server.sleep_inhibitor = -1;
    if (server.session) {
        add_listener(&server.session->events.active, &server.session_active, session_active);
        session_active(&server.session_active, NULL);
    }
#endif

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }

    wlr_renderer_init_wl_shm(server.renderer, server.wl_display);
    // GPU clients (browsers, Electron, games) share buffers by dmabuf; the scene sends them
    // scanout feedback, and explicit sync keeps NVIDIA from showing unfinished frames.
    struct wlr_linux_dmabuf_v1 *linux_dmabuf = NULL;
    if (wlr_renderer_get_texture_formats(server.renderer, WLR_BUFFER_CAP_DMABUF))
        linux_dmabuf =
            wlr_linux_dmabuf_v1_create_with_renderer(server.wl_display, 4, server.renderer);
    int drm_fd = wlr_renderer_get_drm_fd(server.renderer);
    if (drm_fd >= 0 && server.renderer->features.timeline && server.backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(server.wl_display, 1, drm_fd);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    struct wlr_compositor *compositor =
        wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    wlr_primary_selection_v1_device_manager_create(server.wl_display);
    wlr_data_control_manager_v1_create(server.wl_display);
    wlr_ext_data_control_manager_v1_create(server.wl_display, 1);
    wlr_viewporter_create(server.wl_display);
    wlr_fractional_scale_manager_v1_create(server.wl_display, 1);
    wlr_single_pixel_buffer_manager_v1_create(server.wl_display);
    wlr_presentation_create(server.wl_display, server.backend, 2);
    wlr_xdg_wm_dialog_v1_create(server.wl_display, 1);
    // Portals parent their file choosers and share dialogs to the requesting window.
    struct wlr_xdg_foreign_registry *foreign_registry =
        wlr_xdg_foreign_registry_create(server.wl_display);
    wlr_xdg_foreign_v1_create(server.wl_display, foreign_registry);
    wlr_xdg_foreign_v2_create(server.wl_display, foreign_registry);

    server.output_layout = wlr_output_layout_create(server.wl_display);
    wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

    wl_list_init(&server.outputs);
    wl_list_init(&server.disabled_outputs);
    add_listener(&server.backend->events.new_output, &server.new_output, server_new_output);

    server.scene = wlr_scene_create();
    if (linux_dmabuf)
        wlr_scene_set_linux_dmabuf_v1(server.scene, linux_dmabuf);
    wlr_scene_set_gamma_control_manager_v1(server.scene,
                                           wlr_gamma_control_manager_v1_create(server.wl_display));
    // Stacking order, bottom to top.
    struct wlr_scene_tree **stack[] = {
        &server.backgrounds, &server.layer_trees[0], &server.layer_trees[1],
        &server.windows,     &server.fullscreen,     &server.layer_trees[2], &server.fullscreen_cover,
        &server.unmanaged,   &server.guide_layer,    &server.overview_layer, &server.layer_trees[3], &server.drag_icons,
        &server.lock_tree,
    };
    for (size_t i = 0; i < sizeof(stack) / sizeof(stack[0]); ++i)
        *stack[i] = wlr_scene_tree_create(&server.scene->tree);
    server.lock_blanks = wlr_scene_tree_create(server.lock_tree);
    wlr_scene_node_set_enabled(&server.lock_tree->node, false);
    struct wlr_session_lock_manager_v1 *lock_manager =
        wlr_session_lock_manager_v1_create(server.wl_display);
    add_listener(&lock_manager->events.new_lock, &server.new_lock, server_new_lock);
    wl_list_init(&server.layers);
    struct wlr_layer_shell_v1 *layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    add_listener(&layer_shell->events.new_surface, &server.new_layer_surface,
                 server_new_layer_surface);
    server.foreign_manager = wlr_foreign_toplevel_manager_v1_create(server.wl_display);
    // Screen capture for screenshots and portal screen sharing (xdg-desktop-portal-wlr).
    wlr_screencopy_manager_v1_create(server.wl_display);
    wlr_export_dmabuf_manager_v1_create(server.wl_display);
    wlr_ext_image_copy_capture_manager_v1_create(server.wl_display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(server.wl_display, 1);
    server.toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(server.wl_display, 1);
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *toplevel_capture =
        wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(server.wl_display, 1);
    add_listener(&toplevel_capture->events.new_request, &server.new_capture_request,
                 server_new_capture_request);
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    wl_list_init(&server.toplevels);
    struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    add_listener(&xdg_shell->events.new_toplevel, &server.new_xdg_toplevel,
                 server_new_xdg_toplevel);
    add_listener(&xdg_shell->events.new_popup, &server.new_xdg_popup, server_new_xdg_popup);
    struct wlr_xdg_decoration_manager_v1 *decorations =
        wlr_xdg_decoration_manager_v1_create(server.wl_display);
    add_listener(&decorations->events.new_toplevel_decoration, &server.new_decoration,
                 server_new_decoration);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    /* Without a theme, wlroots draws the bare X11 arrow. Adwaita ships with GTK, so it is
     * nearly always installed; $XCURSOR_THEME and $XCURSOR_SIZE still win. Exporting both lets
     * applications draw the same cursor over their windows. */
    const char *cursor_theme = getenv("XCURSOR_THEME");
    if (!cursor_theme || !*cursor_theme)
        cursor_theme = "Adwaita";
    const char *size_env = getenv("XCURSOR_SIZE");
    int cursor_size = size_env ? atoi(size_env) : 0;
    if (cursor_size <= 0)
        cursor_size = 24;
    char size_text[16];
    snprintf(size_text, sizeof(size_text), "%d", cursor_size);
    setenv("XCURSOR_THEME", cursor_theme, true);
    setenv("XCURSOR_SIZE", size_text, true);
    server.cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    add_listener(&server.cursor->events.motion, &server.cursor_motion, server_cursor_motion);
    add_listener(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute,
                 server_cursor_motion_absolute);
    add_listener(&server.cursor->events.button, &server.cursor_button, server_cursor_button);
    add_listener(&server.cursor->events.axis, &server.cursor_axis, server_cursor_axis);
    add_listener(&server.cursor->events.frame, &server.cursor_frame, server_cursor_frame);

    wl_list_init(&server.keyboards);
    wl_list_init(&server.pointers);
    add_listener(&server.backend->events.new_input, &server.new_input, server_new_input);
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboards =
        wlr_virtual_keyboard_manager_v1_create(server.wl_display);
    add_listener(&virtual_keyboards->events.new_virtual_keyboard, &server.new_virtual_keyboard,
                 server_new_virtual_keyboard);
    struct wlr_virtual_pointer_manager_v1 *virtual_pointers =
        wlr_virtual_pointer_manager_v1_create(server.wl_display);
    add_listener(&virtual_pointers->events.new_virtual_pointer, &server.new_virtual_pointer,
                 server_new_virtual_pointer);
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    // Always offered, so clients bind a keyboard even before one (maybe virtual) appears.
    wlr_seat_set_capabilities(server.seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    add_listener(&server.seat->events.request_set_cursor, &server.request_cursor,
                 seat_request_cursor);
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr =
        wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
    add_listener(&cursor_shape_mgr->events.request_set_shape, &server.request_set_shape,
                 cursor_request_set_shape);
    add_listener(&server.seat->pointer_state.events.focus_change, &server.pointer_focus_change,
                 seat_pointer_focus_change);
    add_listener(&server.seat->events.request_set_selection, &server.request_set_selection,
                 seat_request_set_selection);
    add_listener(&server.seat->events.request_set_primary_selection,
                 &server.request_set_primary_selection, seat_request_set_primary_selection);
    add_listener(&server.seat->events.request_start_drag, &server.request_start_drag,
                 seat_request_start_drag);
    add_listener(&server.seat->events.start_drag, &server.start_drag, seat_start_drag);
    add_listener(&server.seat->keyboard_state.events.focus_change, &server.keyboard_focus_change,
                 seat_keyboard_focus_change);
    struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(server.wl_display);
    add_listener(&activation->events.request_activate, &server.request_activate, request_activate);
    server.relative_pointer = wlr_relative_pointer_manager_v1_create(server.wl_display);
    server.constraints = wlr_pointer_constraints_v1_create(server.wl_display);
    add_listener(&server.constraints->events.new_constraint, &server.new_constraint,
                 server_new_constraint);
    server.idle_notifier = wlr_idle_notifier_v1_create(server.wl_display);
    // wlr-randr, kanshi, and graphical display settings tools.
    server.output_manager = wlr_output_manager_v1_create(server.wl_display);
    add_listener(&server.output_manager->events.apply, &server.output_apply, output_config_apply);
    add_listener(&server.output_manager->events.test, &server.output_test, output_config_test);
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit =
        wlr_idle_inhibit_v1_create(server.wl_display);
    add_listener(&idle_inhibit->events.new_inhibitor, &server.new_inhibitor, server_new_inhibitor);

    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    open_control_socket(&server, socket);
    setenv("XDG_CURRENT_DESKTOP", "shaodesk", true);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    // Firefox, Electron (Discord, VS Code), and Java would otherwise need to be told to use
    // Wayland or to cope without a reparenting window manager. The user's own values win.
    setenv("MOZ_ENABLE_WAYLAND", "1", false);
    setenv("ELECTRON_OZONE_PLATFORM_HINT", "auto", false);
    setenv("_JAVA_AWT_WM_NONREPARENTING", "1", false);
    unsetenv("DISPLAY");
#if WLR_HAS_XWAYLAND
    // Xwayland starts when the first X11 client connects and exits once idle.
    if (server_settings(&server)->xwayland) {
        server.xwayland = wlr_xwayland_create(server.wl_display, compositor, true);
        if (server.xwayland) {
            add_listener(&server.xwayland->events.ready, &server.xwayland_ready, xwayland_ready);
            add_listener(&server.xwayland->events.new_surface, &server.new_xwayland_surface,
                         server_new_xwayland_surface);
            setenv("DISPLAY", server.xwayland->display_name, true);
            wlr_log(WLR_INFO, "XWayland listening on DISPLAY=%s", server.xwayland->display_name);
        } else {
            wlr_log(WLR_ERROR, "Cannot create XWayland; X11 applications are unavailable");
        }
    }
#else
    (void)compositor;
#endif
    night_light_update(&server);
    server.running = true;
    callbacks->startup(callbacks->userdata);

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);
    server.running = false;
    wl_event_source_remove(sigint);
    wl_event_source_remove(sigterm);
    wl_event_source_remove(sighup);
    wl_event_source_remove(sigchld);

#if WLR_HAS_XWAYLAND
#if SHAODESK_XWM_WAKER
    close_xwm_waker(&server);
#endif
    if (server.xwayland) {
        wl_list_remove(&server.xwayland_ready.link);
        wl_list_remove(&server.new_xwayland_surface.link);
        wlr_xwayland_destroy(server.xwayland);
    }
#endif
    close_control_socket(&server);
    wl_display_destroy_clients(server.wl_display);

    wl_list_remove(&server.new_xdg_toplevel.link);
    wl_list_remove(&server.new_xdg_popup.link);
    wl_list_remove(&server.new_decoration.link);
    wl_list_remove(&server.new_layer_surface.link);

    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wl_list_remove(&server.cursor_button.link);
    wl_list_remove(&server.cursor_axis.link);
    wl_list_remove(&server.cursor_frame.link);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.new_virtual_keyboard.link);
    wl_list_remove(&server.new_virtual_pointer.link);
    wl_list_remove(&server.request_cursor.link);
    wl_list_remove(&server.request_set_shape.link);
    wl_list_remove(&server.pointer_focus_change.link);
    wl_list_remove(&server.request_set_selection.link);
    wl_list_remove(&server.request_set_primary_selection.link);
    wl_list_remove(&server.request_start_drag.link);
    wl_list_remove(&server.start_drag.link);
    wl_list_remove(&server.keyboard_focus_change.link);
    wl_list_remove(&server.request_activate.link);
    wl_list_remove(&server.new_constraint.link);
    wl_list_remove(&server.new_capture_request.link);

    wl_list_remove(&server.new_output.link);
    wl_list_remove(&server.new_lock.link);
    wl_list_remove(&server.new_inhibitor.link);
    wl_list_remove(&server.output_apply.link);
    wl_list_remove(&server.output_test.link);
#if WLR_HAS_SESSION
    if (server.session)
        wl_list_remove(&server.session_active.link);
    if (server.sleep_inhibitor >= 0)
        close(server.sleep_inhibitor);
#endif

    if (server.night_timer)
        wl_event_source_remove(server.night_timer);
    if (server.corner_timer)
        wl_event_source_remove(server.corner_timer);
    if (server.overview.timer)
        wl_event_source_remove(server.overview.timer);
    if (server.urgent_timer)
        wl_event_source_remove(server.urgent_timer);
    if (server.config_watch)
        wl_event_source_remove(server.config_watch);
    if (server.config_timer)
        wl_event_source_remove(server.config_timer);
    if (server.night_transform)
        wlr_color_transform_unref(server.night_transform);
    wlr_backend_destroy(server.backend);
    sh_animator_destroy(server.animator);
    wlr_scene_node_destroy(&server.scene->tree.node);
    for (size_t i = 0; i < sizeof(server.deco_buffers) / sizeof(*server.deco_buffers); ++i)
        wlr_buffer_drop(server.deco_buffers[i]);
    wlr_buffer_drop(server.black);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wl_display_destroy(server.wl_display);
    sh_tiling_destroy(server.tiling);
    return 0;
}
