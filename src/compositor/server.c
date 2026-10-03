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

/* BEGIN FORWARD */
static struct wlr_scene_tree *fullscreen_tree(struct sh_toplevel *toplevel);
static void swallow_release(struct sh_toplevel *child);
static void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output);
static void reload_config(struct sh_server *server);
/* END FORWARD */

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

struct wlr_output *first_output(struct sh_server *server) {
    if (wl_list_empty(&server->outputs))
        return NULL;
    struct sh_output *first = wl_container_of(server->outputs.next, first, link);
    return first->wlr_output;
}

/* The current workspace of the output named `name`, which need not be connected. */
int output_slot(struct sh_server *server, const char *name) {
    int count = sizeof(server->output_workspaces) / sizeof(server->output_workspaces[0]);
    int unused = -1, gone = -1;
    for (int i = 0; i < count; ++i) {
        const char *known = server->output_workspaces[i].name;
        if (!strcmp(known, name))
            return i;
        if (!known[0] && unused < 0)
            unused = i;
        else if (known[0] && gone < 0 && !find_output(server, known))
            gone = i;
    }
    int slot = unused >= 0 ? unused : gone >= 0 ? gone : 0;
    snprintf(server->output_workspaces[slot].name, sizeof(server->output_workspaces[slot].name),
             "%s", name);
    server->output_workspaces[slot].current = 0;
    server->output_workspaces[slot].previous = -1;
    server->output_workspaces[slot].tiling = -1;
    memset(server->output_workspaces[slot].workspace_tiling, -1,
           sizeof(server->output_workspaces[slot].workspace_tiling));
    return slot;
}

int *output_workspace(struct sh_server *server, const char *name) {
    return &server->output_workspaces[output_slot(server, name)].current;
}

/* A window shows when its output shows its workspace. The output may be gone: its windows
 * keep their state until they are placed on another output. */
bool toplevel_visible(struct sh_toplevel *toplevel) {
    return !toplevel->minimized && !toplevel->group_hidden && !toplevel->swallowed &&
           (toplevel->sticky || !toplevel->output[0] ||
            toplevel->workspace == *output_workspace(toplevel->server, toplevel->output));
}

/* Shows each output's current workspace; focus is left to the caller. Sticky windows move
 * along to it. */
void show_workspaces(struct sh_server *server) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->sticky && toplevel->output[0])
            toplevel->workspace = *output_workspace(server, toplevel->output);
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
    }
    notify_subscribers(server);
}

void set_active_output(struct sh_server *server, const char *name) {
    if (!name[0] || !strcmp(server->active_output, name))
        return;
    snprintf(server->active_output, sizeof(server->active_output), "%s", name);
    notify_subscribers(server);
}

/* Every workspace switch comes through here (actions, the panel, taskbar activation), so each
 * output remembers the workspace it showed before for workspace_back. */
void show_workspace(struct sh_server *server, const char *output, int workspace) {
    int slot = output_slot(server, output);
    if (server->output_workspaces[slot].current != workspace)
        server->output_workspaces[slot].previous = server->output_workspaces[slot].current;
    server->output_workspaces[slot].current = workspace;
    wlr_log(WLR_INFO, "Workspace %d on %s", workspace + 1, output);
    show_workspaces(server);
    // The output's sticky windows come up over the workspace's own, keeping their order.
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (toplevel->sticky && !strcmp(toplevel->output, output))
            wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    }
}

/* The output workspace actions apply to: the one a control request names, else the one last
 * focused (by focusing a window there, switching its workspace, or clicking on it), else the
 * one under the pointer. */
struct wlr_output *focused_output(struct sh_server *server) {
    if (server->target_output)
        return server->target_output;
    struct wlr_output *output = find_output(server, server->active_output);
    if (!output)
        output = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
                                             server->cursor->y);
    return output ? output : first_output(server);
}

/* A window placed on another output joins that output's current workspace. */
void set_toplevel_output(struct sh_toplevel *toplevel, struct wlr_output *output) {
    if (!output || !strcmp(toplevel->output, output->name))
        return;
    snprintf(toplevel->output, sizeof(toplevel->output), "%s", output->name);
    toplevel->workspace = *output_workspace(toplevel->server, output->name);
    toplevel->home_output[0] = '\0'; // placed on another output by hand: it stays
    group_follow(toplevel);
    if (toplevel->server->focused_toplevel == toplevel)
        set_active_output(toplevel->server, output->name);
    if (toplevel_mapped(toplevel)) {
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
        notify_subscribers(toplevel->server);
    }
}

/* Floating windows belong to the output their centre is on, wherever they were moved from:
 * the pointer, a snap, or the client. Tiles belong to the output of their tiling. */
void follow_output(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return;
#endif
    if (toplevel->tiled || !toplevel_mapped(toplevel))
        return;
    struct wlr_box box = toplevel_box(toplevel);
    set_toplevel_output(toplevel, wlr_output_layout_output_at(toplevel->server->output_layout,
                                                              box.x + box.width / 2.0,
                                                              box.y + box.height / 2.0));
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

static void focus_urgent(struct sh_server *server) {
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
static void focus_last(struct sh_server *server) {
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
static void focus_top_on(struct sh_server *server, struct wlr_output *output) {
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

void minimize_toplevel(struct sh_toplevel *toplevel) {
    group_detach(toplevel); // a minimized window keeps no slot to share
    toplevel->minimized = true;
    untile_toplevel(toplevel, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, true);
    if (toplevel->server->focused_toplevel == toplevel)
        focus_previous(toplevel->server);
}

struct sh_rect usable_area(struct sh_server *server, struct wlr_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output, &box);
    struct sh_output *candidate;
    wl_list_for_each(candidate, &server->outputs, link) {
        if (candidate->wlr_output == output)
            box = candidate->usable;
    }
    return (struct sh_rect){box.x, box.y, box.width, box.height};
}

/* Fullscreen the client asked for covers the whole output; fullscreen from a binding or the
 * title bar leaves the panels' exclusive zones shown. */
struct wlr_box fullscreen_box(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct wlr_box box;
    if (toplevel->fullscreen_cover) {
        wlr_output_layout_get_box(toplevel->server->output_layout, output, &box);
        return box;
    }
    struct sh_rect area = usable_area(toplevel->server, output);
    return (struct wlr_box){area.x, area.y, area.width, area.height};
}

static struct wlr_scene_tree *fullscreen_tree(struct sh_toplevel *toplevel) {
    return toplevel->fullscreen_cover ? toplevel->server->fullscreen_cover
                                      : toplevel->server->fullscreen;
}

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

/* A tiled window moves into the tiling of the same output on its new workspace, or floats when
 * that workspace does not tile; a window that floated only because its workspace did not tile
 * joins the tiling of one that does. */
static void set_toplevel_workspace(struct sh_toplevel *toplevel, int workspace) {
    bool was_tiled = toplevel->tiled;
    struct wlr_output *output = tiled_output(toplevel);
    bool retile = was_tiled && (!output || workspace_tiles(toplevel->server, output, workspace));
    untile_toplevel(toplevel, was_tiled && !retile);
    toplevel->workspace = workspace;
    group_follow(toplevel);
    if (retile)
        tile_toplevel(toplevel, output, NULL, false);
    else if (!was_tiled && toplevel_mapped(toplevel) && wants_tiling(toplevel, NULL))
        tile_toplevel(toplevel, NULL, NULL, false);
    notify_subscribers(toplevel->server);
}

/* The distance windows slide when `output`'s workspace changes. */
static int slide_distance(struct sh_server *server, struct wlr_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output, &box);
    return (int)(box.width * server_settings(server)->animation_slide);
}

/* Windows that come and go with a workspace, not sticky ones, which stay. */
static bool slides(struct sh_server *server, struct sh_toplevel *toplevel,
                   struct wlr_output *output) {
    return toplevel->shown && toplevel_mapped(toplevel) && !toplevel->sticky &&
           !strcmp(toplevel->output, output->name) && server->running;
}

/* Before the switch: copies of the visible windows slide away in `direction` (-1 is left). */
static void slide_out_workspace(struct sh_server *server, struct wlr_output *output, int direction) {
    int distance = slide_distance(server, output);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!slides(server, toplevel, output) || !toplevel_visible(toplevel))
            continue;
        sh_anim_slide_out(server->animator, &toplevel->scene_tree->node, toplevel->content,
                          direction * distance, 0);
    }
}

/* After the switch: the windows now shown arrive from the side the old ones left toward. */
static void slide_in_workspace(struct sh_server *server, struct wlr_output *output, int direction) {
    int distance = slide_distance(server, output);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (slides(server, toplevel, output) && toplevel_visible(toplevel))
            sh_anim_slide(server->animator, &toplevel->anim, toplevel->content,
                          direction * distance, 0);
    }
}

/* Switches only `output`. Focus moves along when it was on that output (or nowhere), so paging
 * another monitor from its panel leaves the focused window alone. */
void switch_workspace(struct sh_server *server, struct wlr_output *output, int workspace) {
    int count = server_settings(server)->workspaces;
    if (!output || workspace < 0 || workspace >= count ||
        workspace == *output_workspace(server, output->name))
        return;
    if (server->grabbed_toplevel)
        reset_cursor_mode(server);
    struct sh_toplevel *focused = server->focused_toplevel;
    bool refocus = !focused || find_output(server, focused->output) == output;
    if (refocus)
        deactivate_toplevel(server);
    int from = *output_workspace(server, output->name);
    slide_out_workspace(server, output, workspace > from ? -1 : 1);
    show_workspace(server, output->name, workspace);
    slide_in_workspace(server, output, workspace > from ? 1 : -1);
    if (refocus) {
        focus_top_on(server, output);
        set_active_output(server, output->name);
    }
}

/* Makes the window sticky, floating it, or returns it to how it floated or tiled before.
 * Without `retile`, the caller puts a window that tiled before back into the tiling. */
void set_sticky(struct sh_toplevel *toplevel, bool sticky, bool retile) {
    struct sh_server *server = toplevel->server;
    if (toplevel->sticky == sticky)
        return;
    toplevel->sticky = sticky;
    if (sticky) {
        toplevel->sticky_floating = toplevel->floating;
        toplevel->floating = true;
        if (toplevel->tiled) {
            toplevel->placed = false;
            untile_toplevel(toplevel, true);
        }
        if (toplevel->output[0])
            toplevel->workspace = *output_workspace(server, toplevel->output);
    } else {
        toplevel->floating = toplevel->sticky_floating;
        if (retile && wants_tiling(toplevel, NULL))
            tile_toplevel(toplevel, NULL, NULL, false);
    }
    wlr_log(WLR_INFO, "Window %s", sticky ? "sticky" : "no longer sticky");
    notify_subscribers(server);
}

/* A sticky window moved to a workspace stops being sticky and stays there. */
static void move_toplevel_to_workspace(struct sh_server *server, struct sh_toplevel *toplevel,
                                       int workspace) {
    int count = server_settings(server)->workspaces;
    if (!toplevel || workspace < 0 || workspace >= count)
        return;
    bool was_sticky = toplevel->sticky;
    set_sticky(toplevel, false, workspace == toplevel->workspace);
    if (workspace == toplevel->workspace)
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    toplevel->scratchpad = false; // it belongs to that workspace now
    set_toplevel_workspace(toplevel, workspace);
    if (was_sticky && wants_tiling(toplevel, NULL))
        tile_toplevel(toplevel, NULL, NULL, false);
    bool visible = toplevel_visible(toplevel); // a window whose output is gone stays visible
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
    if (server->focused_toplevel == toplevel && !visible) {
        deactivate_toplevel(server);
        focus_top_on(server, find_output(server, toplevel->output));
    }
}

static void move_to_workspace(struct sh_server *server, int workspace) {
    move_toplevel_to_workspace(server, current_toplevel(server), workspace);
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
static void focus_direction(struct sh_server *server, enum sh_action action) {
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

/* Window groups. The member showing holds the group's slot (its tile, or its floating place);
 * the others are group_hidden, in no tiling and on no screen, until a tab brings one forward. */
bool groups_enabled(struct sh_server *server) {
    return server_settings(server)->groups;
}

int group_size(struct sh_server *server, unsigned group) {
    int count = 0;
    struct sh_toplevel *member;
    if (group)
        wl_list_for_each(member, &server->toplevels, link) count += member->group == group;
    return count;
}

/* The member of `group` that is showing. */
static struct sh_toplevel *group_shown(struct sh_server *server, unsigned group) {
    struct sh_toplevel *member;
    if (group)
        wl_list_for_each(member, &server->toplevels, link) {
            if (member->group == group && !member->group_hidden)
                return member;
        }
    return NULL;
}

/* The tab `from` is, counting from 0. */
int group_index(struct sh_toplevel *from) {
    int index = 0;
    struct sh_toplevel *member;
    wl_list_for_each(member, &from->server->toplevels, link) {
        index += member->group == from->group && member->group_order < from->group_order;
    }
    return index;
}

/* The member `step` (1 or -1) tabs away from `from`, wrapping; NULL when it is alone. */
static struct sh_toplevel *group_step(struct sh_toplevel *from, int step) {
    struct sh_toplevel *member, *best = NULL, *wrap = NULL;
    wl_list_for_each(member, &from->server->toplevels, link) {
        if (member->group != from->group || member == from)
            continue;
        bool beyond = step > 0 ? member->group_order > from->group_order
                               : member->group_order < from->group_order;
        if (beyond && (!best || (step > 0 ? member->group_order < best->group_order
                                          : member->group_order > best->group_order)))
            best = member;
        if (!wrap || (step > 0 ? member->group_order < wrap->group_order
                               : member->group_order > wrap->group_order))
            wrap = member;
    }
    return best ? best : wrap;
}

/* Hidden members follow the shown one to another workspace or output. */
void group_follow(struct sh_toplevel *toplevel) {
    if (!toplevel->group || toplevel->group_hidden)
        return;
    struct sh_toplevel *member;
    wl_list_for_each(member, &toplevel->server->toplevels, link) {
        if (member->group != toplevel->group || member == toplevel)
            continue;
        member->workspace = toplevel->workspace;
        snprintf(member->output, sizeof(member->output), "%s", toplevel->output);
    }
}

/* `to` takes the slot of `from` (its tile, or its floating place and state). The caller has set
 * which of the two is hidden. */
static void hand_over_slot(struct sh_toplevel *from, struct sh_toplevel *to) {
    struct sh_server *server = from->server;
    struct wlr_output *output = from->tiled ? tiled_output(from) : NULL;
    to->workspace = from->workspace;
    snprintf(to->output, sizeof(to->output), "%s", from->output);
    to->floating = from->floating;
    to->placed = from->placed;
    to->restore_box = from->restore_box;
    to->tile_sized = false;
    wlr_scene_node_set_position(&to->scene_tree->node, from->scene_tree->node.x,
                                from->scene_tree->node.y);
    if (from->tiled) {
        sh_tiling_replace(server->tiling, from, to);
        from->tiled = false;
        from->arranged = false;
        from->arrangement = SH_NONE;
        to->tiled = true;
        to->arranged = false;
        to->arrangement = SH_NONE;
        if (to->foreign)
            wlr_foreign_toplevel_handle_v1_set_maximized(to->foreign, false);
    } else {
        struct wlr_box box = toplevel_box(from);
        to->arranged = from->arranged;
        to->arrangement = from->arrangement;
        toplevel_set_states(to, from->arrangement == SH_MAXIMIZE, 0);
        toplevel_configure_box(to, box);
    }
    wlr_scene_node_set_enabled(&from->scene_tree->node, toplevel_visible(from));
    wlr_scene_node_set_enabled(&to->scene_tree->node, toplevel_visible(to));
    if (output)
        reflow_output(server, output);
    refresh_frame(from);
    refresh_frame(to);
}

/* `to` (a hidden member) takes the slot of `from` (the member showing), which hides. */
static void group_take_slot(struct sh_toplevel *from, struct sh_toplevel *to) {
    from->group_hidden = true;
    to->group_hidden = false;
    hand_over_slot(from, to);
    group_follow(to);
    notify_subscribers(from->server);
}

/* Brings a hidden member forward; the caller focuses it. */
void group_show(struct sh_toplevel *toplevel) {
    struct sh_toplevel *shown = group_shown(toplevel->server, toplevel->group);
    if (!toplevel->group_hidden || !shown || shown == toplevel)
        return;
    if (shown->fullscreen)
        set_fullscreen(shown, false); // it could not hide, and would return fullscreen
    group_take_slot(shown, toplevel);
    // The tabs of the others follow the new count and highlight.
    struct sh_toplevel *member;
    wl_list_for_each(member, &toplevel->server->toplevels, link) {
        if (member->group == toplevel->group)
            refresh_tabs(member);
    }
}

/* Takes a window out of its group. A member showing hands its slot to the next tab; a group
 * left with one window dissolves. The window keeps no slot of its own (it is on its way out,
 * or the caller places it). */
void group_detach(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    unsigned group = toplevel->group;
    if (!group)
        return;
    struct sh_toplevel *heir = toplevel->group_hidden ? NULL : group_step(toplevel, 1);
    bool had_focus = server->focused_toplevel == toplevel;
    if (heir)
        group_take_slot(toplevel, heir);
    toplevel->group = 0;
    toplevel->group_hidden = false;
    if (toplevel->tabs) {
        wlr_scene_node_destroy(&toplevel->tabs->node);
        toplevel->tabs = NULL;
    }
    if (server->tabs_hovered == toplevel)
        server->tabs_hovered = NULL;
    struct sh_toplevel *member, *last = NULL;
    int left = 0;
    wl_list_for_each(member, &server->toplevels, link) {
        if (member->group == group)
            ++left, last = member;
    }
    if (heir && had_focus)
        focus_toplevel(heir);
    if (left == 1 && last) {
        last->group = 0;
        refresh_tabs(last);
    } else if (left > 1) {
        wl_list_for_each(member, &server->toplevels, link) {
            if (member->group == group)
                refresh_tabs(member);
        }
    }
    notify_subscribers(server);
}

/* Whether a window can be part of a group: an ordinary window on a workspace. */
bool groupable(struct sh_toplevel *toplevel) {
    return toplevel && toplevel_mapped(toplevel) && !toplevel->sticky && !toplevel->scratchpad &&
           !toplevel->fullscreen && !toplevel->minimized && !toplevel->group_hidden
#if WLR_HAS_XWAYLAND
           && !toplevel->unmanaged
#endif
        ;
}

/* Makes `toplevel` a member of `group`, last in tab order. */
void group_join(struct sh_toplevel *toplevel, unsigned group) {
    toplevel->group = group;
    toplevel->group_order = ++toplevel->server->group_serial;
}

/* The focused window becomes a group of one, so windows opening next join it, or, in a group,
 * the whole group dissolves: hidden members return to tiles beside the shown one. */
static void group_toggle(struct sh_server *server, struct sh_toplevel *current) {
    if (!current)
        return;
    if (!current->group) {
        if (!groupable(current))
            return;
        group_join(current, ++server->group_serial);
        refresh_frame(current);
        notify_subscribers(server);
        return;
    }
    unsigned group = current->group;
    struct sh_toplevel *member, *tmp;
    struct sh_toplevel *shown = group_shown(server, group);
    wl_list_for_each_safe(member, tmp, &server->toplevels, link) {
        if (member->group != group)
            continue;
        bool hidden = member->group_hidden;
        member->group = 0;
        member->group_hidden = false;
        if (hidden && shown) {
            // It comes back beside the shown window: a tile splitting its slot, or floating there.
            wlr_scene_node_set_enabled(&member->scene_tree->node, toplevel_visible(member));
            if (wants_tiling(member, NULL) && shown->tiled)
                tile_toplevel_at(member, tiled_output(shown), shown, false, 0, 0);
            else if (!member->tiled) {
                struct wlr_box box = toplevel_box(shown);
                box.x += 32, box.y += 32;
                toplevel_configure_box(member, box);
            }
        }
        refresh_frame(member);
    }
    notify_subscribers(server);
}

static void group_cycle(struct sh_server *server, struct sh_toplevel *current, int step) {
    if (!current || !current->group)
        return;
    struct sh_toplevel *next = group_step(current, step);
    if (!next)
        return;
    focus_toplevel(next); // a hidden member takes the slot as it gets focus
}

/* Takes the focused window out of its group into a slot of its own beside it. */
static void ungroup(struct sh_server *server, struct sh_toplevel *current) {
    if (!current || !current->group)
        return;
    struct sh_toplevel *heir = current->group_hidden ? NULL : group_step(current, 1);
    struct wlr_output *output = current->tiled ? tiled_output(current) : NULL;
    struct wlr_box box = toplevel_box(current);
    bool floating = !current->tiled;
    group_detach(current);
    if (!heir || current->group_hidden)
        return;
    // The heir holds the slot now; the window gets a place beside it.
    wlr_scene_node_set_enabled(&current->scene_tree->node, toplevel_visible(current));
    if (output && wants_tiling(current, output)) {
        tile_toplevel_at(current, output, heir, false, 0, 0);
    } else if (floating) {
        box.x += 32, box.y += 32;
        toplevel_configure_box(current, box);
    }
    refresh_frame(current);
    focus_toplevel(current);
}

/* Moves the focused window into the group of the window beside it, that way: it becomes the
 * shown tab in that window's slot. */
static void group_merge(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *current = server->focused_toplevel;
    if (server->locked || !groupable(current))
        return;
    int index = action - SH_GROUP_MERGE_LEFT;
    bool horizontal = index < 2;
    int sign = index % 2 ? 1 : -1;
    struct sh_toplevel *target = toplevel_toward(current, horizontal, sign, false);
    if (!target || target == current || !groupable(target) || (target->group && target->group == current->group))
        return;
    if (!target->group)
        group_join(target, ++server->group_serial);
    unsigned group = target->group;
    // Leave the old group (or slot), then take the target's slot as its shown tab.
    group_detach(current);
    if (current->tiled)
        untile_toplevel(current, false);
    group_join(current, group);
    current->group_hidden = true; // hidden until it takes the slot, so the swap is uniform
    wlr_scene_node_set_enabled(&current->scene_tree->node, false);
    struct sh_toplevel *member;
    group_show(current);
    focus_toplevel(current);
    wl_list_for_each(member, &server->toplevels, link) {
        if (member->group == group)
            refresh_tabs(member);
    }
}

/* With features.groups off every group dissolves. */
static void dissolve_groups(struct sh_server *server) {
    struct sh_toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
        if (toplevel->group && !toplevel->group_hidden)
            group_toggle(server, toplevel);
    }
    wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
        if (toplevel->group)
            group_toggle(server, toplevel);
    }
}

/* Window swallowing (windows.swallow). A window started from a terminal, which the process
 * ancestry shows, takes the terminal's slot and hides it; when the window closes, the terminal
 * takes the slot back. */
static bool swallow_listed(const char (*names)[64], int count, const char *name) {
    for (int i = 0; name && i < count; ++i) {
        if (!strcasecmp(names[i], name))
            return true;
    }
    return false;
}

static bool swallow_terminal(struct sh_toplevel *toplevel) {
    const struct sh_settings *settings = server_settings(toplevel->server);
    return swallow_listed(settings->swallow_terminals, settings->swallow_terminal_count,
                          toplevel_app_id(toplevel));
}

/* The parent of a process, or 0 when it cannot be read. */
static pid_t process_parent(pid_t pid) {
    char path[64], text[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[length] = '\0';
    // "pid (name) state ppid ...": the name may hold spaces and parentheses.
    char *end = strrchr(text, ')');
    char state;
    int parent = 0;
    if (!end || sscanf(end + 1, " %c %d", &state, &parent) != 2)
        return 0;
    return parent;
}

/* A window that can lend its slot to another. */
static bool swallow_hostable(struct sh_toplevel *host) {
    return toplevel_mapped(host) && !host->swallowed && !host->swallow_peer && !host->group &&
           !host->minimized && !host->scratchpad && !host->sticky
#if WLR_HAS_XWAYLAND
           && !host->unmanaged
#endif
        ;
}

/* The window `child` was started from: that of its nearest ancestor process which has one
 * (the focused or else the most recently focused, when a process has several). With
 * `terminals_only` it has to be one of the configured terminals. */
struct sh_toplevel *swallow_host(struct sh_toplevel *child, bool terminals_only) {
    struct sh_server *server = child->server;
    pid_t pid = toplevel_pid(child), self = getpid();
    if (pid <= 1)
        return NULL;
    pid = process_parent(pid);
    for (int depth = 0; pid > 1 && pid != self && depth < 64; ++depth, pid = process_parent(pid)) {
        struct sh_toplevel *best = NULL, *host;
        wl_list_for_each(host, &server->toplevels, link) {
            if (host == child || !swallow_hostable(host) || toplevel_pid(host) != pid ||
                (terminals_only && !swallow_terminal(host)))
                continue;
            if (!best || host == server->focused_toplevel)
                best = host;
        }
        if (best)
            return best;
    }
    return NULL;
}

/* Whether a window opening now may swallow its terminal by itself. */
bool swallow_wanted(struct sh_toplevel *child) {
    const struct sh_settings *settings = server_settings(child->server);
    return settings->swallow && !swallow_terminal(child) && !toplevel_is_dialog(child) &&
           !swallow_listed(settings->swallow_exceptions, settings->swallow_exception_count,
                           toplevel_app_id(child));
}

/* `child` takes the place of `host`, which hides and leaves the taskbar. */
void swallow_attach(struct sh_toplevel *host, struct sh_toplevel *child) {
    struct sh_server *server = host->server;
    if (host->fullscreen)
        set_fullscreen(host, false); // it could not hide, and would return fullscreen
    if (child->tiled)
        untile_toplevel(child, false);
    host->swallowed = true;
    host->swallow_peer = child;
    child->swallow_peer = host;
    hand_over_slot(host, child);
    unpublish_toplevel(host);
    if (server->switcher.open)
        switcher_forget(host);
    overview_forget(host);
    notify_subscribers(server);
}

/* The window `child` is going away: the terminal it swallowed takes its place again. */
static void swallow_restore(struct sh_toplevel *child) {
    struct sh_toplevel *host = child->swallow_peer;
    child->swallow_peer = NULL;
    if (!host)
        return;
    struct sh_server *server = child->server;
    host->swallow_peer = NULL;
    host->swallowed = false;
    if (child->fullscreen)
        set_fullscreen(child, false);
    bool had_focus = server->focused_toplevel == child;
    hand_over_slot(child, host);
    publish_toplevel(host);
    if (had_focus)
        focus_toplevel(host);
    notify_subscribers(server);
}

/* The window `child` stays; the terminal it swallowed comes back beside it. */
static void swallow_release(struct sh_toplevel *child) {
    struct sh_toplevel *host = child->swallow_peer;
    child->swallow_peer = NULL;
    if (!host)
        return;
    struct sh_server *server = child->server;
    host->swallow_peer = NULL;
    host->swallowed = false;
    publish_toplevel(host);
    wlr_scene_node_set_enabled(&host->scene_tree->node, toplevel_visible(host));
    struct wlr_output *output = child->tiled ? tiled_output(child) : NULL;
    if (output && wants_tiling(host, output)) {
        tile_toplevel_at(host, output, child, false, 0, 0);
    } else if (!host->tiled) {
        struct wlr_box box = toplevel_box(child);
        box.x += 32, box.y += 32;
        toplevel_configure_box(host, box);
    }
    refresh_frame(host);
    notify_subscribers(server);
}

/* A window that swallowed or was swallowed is unmapping. */
void swallow_end(struct sh_toplevel *toplevel) {
    struct sh_toplevel *peer = toplevel->swallow_peer;
    if (!peer)
        return;
    if (toplevel->swallowed) {
        // The terminal closed: the window that took its place stays where it is.
        peer->swallow_peer = NULL;
        toplevel->swallow_peer = NULL;
        toplevel->swallowed = false;
    } else {
        swallow_restore(toplevel);
    }
}

/* swallow_toggle: the focused window gives its terminal a place again, or, when it has none
 * swallowed, takes the place of the terminal it was started from (else of the terminal that
 * was focused last on its workspace). */
static void swallow_toggle(struct sh_server *server, struct sh_toplevel *current) {
    if (!current || server->locked || !toplevel_mapped(current) || !toplevel_visible(current))
        return;
    if (current->swallow_peer) {
        swallow_release(current);
        return;
    }
    struct sh_toplevel *host = swallow_host(current, false), *other;
    if (!host) {
        wl_list_for_each(other, &server->toplevels, link) {
            if (other != current && swallow_hostable(other) && swallow_terminal(other) &&
                toplevel_visible(other) && other->workspace == current->workspace &&
                !strcmp(other->output, current->output)) {
                host = other;
                break;
            }
        }
    }
    if (!host)
        return;
    swallow_attach(host, current);
    focus_toplevel(current);
}

/* The window switcher. Subscribers get "switcher OUTPUT SELECTED COUNT" followed by COUNT
 * lines "switcher-window APP_ID\tTITLE\tOUTPUT\tWORKSPACE\tMINIMIZED\tURGENT" as it opens or its list
 * changes, "switcher-select N" as the selection moves (both counting from 0), and
 * "switcher-close" when it closes. */
static void switcher_announce(struct sh_server *server) {
    size_t size = 128 + (size_t)server->switcher.count * 1024, length = 0;
    char *text = malloc(size);
    if (!text)
        return;
    length += snprintf(text, size, "switcher %s %d %d\n", server->switcher.output,
                       server->switcher.selected, server->switcher.count);
    for (int i = 0; i < server->switcher.count; ++i) {
        struct sh_toplevel *toplevel = server->switcher.windows[i];
        char app_id[256], title[512];
        const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        length +=
            snprintf(text + length, size - length, "switcher-window %s\t%s\t%s\t%d\t%d\t%d\n",
                     app_id, title, toplevel->output, toplevel->workspace + 1, toplevel->minimized,
                     toplevel->urgent);
    }
    send_event(server, text, length);
    free(text);
}

static void switcher_select(struct sh_server *server, int selected) {
    int count = server->switcher.count;
    server->switcher.selected = ((selected % count) + count) % count;
    char line[32];
    int length = snprintf(line, sizeof(line), "switcher-select %d\n", server->switcher.selected);
    send_event(server, line, (size_t)length);
}

/* Focuses the window at `index` in the list (or none, below 0) and closes the switcher. */
void switcher_close(struct sh_server *server, int index) {
    if (!server->switcher.open)
        return;
    struct sh_toplevel *chosen =
        index >= 0 && index < server->switcher.count ? server->switcher.windows[index] : NULL;
    server->switcher.open = false;
    server->switcher.count = 0;
    send_event(server, "switcher-close\n", strlen("switcher-close\n"));
    if (!chosen || server->locked)
        return;
    focus_toplevel(chosen);
    // Focus following the mouse would hand focus back to whatever the pointer is over. Raised,
    // the window is on top wherever the pointer is inside it.
    struct wlr_box box = toplevel_box(chosen);
    if (!wlr_box_contains_point(&box, server->cursor->x, server->cursor->y))
        pointer_follow(chosen);
}

/* Opens the switcher on the focused output, selecting the window focused before the current
 * one (or the least recent one, going `backward`); when open, moves the selection instead. */
static void switcher_open(struct sh_server *server, bool backward, uint32_t modifiers,
                          xkb_keysym_t key) {
    if (server->switcher.open) {
        switcher_select(server, server->switcher.selected + (backward ? -1 : 1));
        return;
    }
    struct wlr_output *output = focused_output(server);
    if (server->locked || !output)
        return;
    int count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (toplevel->swallowed)
            continue;
        if (count < (int)(sizeof(server->switcher.windows) / sizeof(*server->switcher.windows)))
            server->switcher.windows[count++] = toplevel;
    }
    if (!count)
        return;
    server->switcher.open = true;
    server->switcher.count = count;
    server->switcher.modifiers = modifiers;
    server->switcher.key = key;
    snprintf(server->switcher.output, sizeof(server->switcher.output), "%s", output->name);
    // The first window is the focused one unless focus is on the bare desktop.
    int next = server->switcher.windows[0] == server->focused_toplevel && count > 1 ? 1 : 0;
    server->switcher.selected = backward ? count - 1 : next;
    switcher_announce(server);
}

/* A window closing while the switcher is open leaves its list. */
void switcher_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server->switcher.open)
        return;
    for (int i = 0; i < server->switcher.count; ++i) {
        if (server->switcher.windows[i] != toplevel)
            continue;
        memmove(&server->switcher.windows[i], &server->switcher.windows[i + 1],
                (size_t)(server->switcher.count - i - 1) * sizeof(*server->switcher.windows));
        if (--server->switcher.count == 0) {
            switcher_close(server, -1);
            return;
        }
        if (server->switcher.selected > i || server->switcher.selected == server->switcher.count)
            server->switcher.selected--;
        switcher_announce(server);
        return;
    }
}

/* Keys while the switcher is open: the opening key (with Shift, backward), Tab, and the arrows
 * move the selection, Return confirms, Escape cancels. It keeps every key from the windows. */
static void switcher_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        switcher_close(server, -1);
        return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        switcher_close(server, server->switcher.selected);
        return;
    case XKB_KEY_ISO_Left_Tab:
    case XKB_KEY_Left:
    case XKB_KEY_Up:
        switcher_select(server, server->switcher.selected - 1);
        return;
    case XKB_KEY_Right:
    case XKB_KEY_Down:
        switcher_select(server, server->switcher.selected + 1);
        return;
    }
    if (sym == XKB_KEY_Tab || xkb_keysym_to_lower(sym) == server->switcher.key)
        switcher_select(server,
                        server->switcher.selected + (modifiers & WLR_MODIFIER_SHIFT ? -1 : 1));
}

/* The overview (Expose). Opening it lays every window of the focused output's workspace out as
 * a live thumbnail in a grid, with a strip of the output's workspaces above. It is drawn by the
 * compositor from scaled copies of the windows' scene nodes (overview_scene.h), so the
 * thumbnails move with the windows' contents at no cost while nothing changes. The shell
 * draws the text (titles, the filter) over it from the events sent by overview_announce. The
 * overview takes the keyboard and the pointer while open and changes nothing until a window
 * is picked, a workspace chosen, or a thumbnail dropped on the strip. */
static void overview_colour(float out[4], float r, float g, float b, float a) {
    out[0] = r * a; // scene rectangles take premultiplied colours
    out[1] = g * a;
    out[2] = b * a;
    out[3] = a;
}

static void overview_thumb_clear(struct sh_thumb *thumb) {
    if (thumb->tree)
        wlr_scene_node_destroy(&thumb->tree->node);
    memset(thumb, 0, sizeof(*thumb));
}

/* Places `toplevel`'s thumbnail with its top-left corner at (x, y), copying the window again
 * only when it changed or the scale did. */
static void overview_thumb_place(struct sh_thumb *thumb, struct wlr_scene_tree *parent,
                                 struct sh_toplevel *toplevel, int x, int y, double scale) {
    if (thumb->tree && thumb->toplevel != toplevel) {
        sh_thumb_clear(thumb->tree);
        thumb->fingerprint = 0;
        thumb->scale = 0;
    }
    thumb->toplevel = toplevel;
    if (!thumb->tree) {
        thumb->tree = wlr_scene_tree_create(parent);
        if (!thumb->tree)
            return;
    }
    uint64_t print = sh_thumb_fingerprint(toplevel->scene_tree);
    if (print != thumb->fingerprint || fabs(scale - thumb->scale) > 1e-4) {
        sh_thumb_clear(thumb->tree);
        sh_thumb_clone(thumb->tree, toplevel->scene_tree, scale, 1.0f);
        thumb->fingerprint = print;
        thumb->scale = scale;
    }
    wlr_scene_node_set_position(&thumb->tree->node, x, y);
    wlr_scene_node_set_enabled(&thumb->tree->node, true);
}

static void overview_rect_set(struct wlr_scene_rect **rect, struct wlr_scene_tree *parent,
                              struct sh_rect box, const float colour[4]) {
    if (!*rect)
        *rect = wlr_scene_rect_create(parent, box.width, box.height, colour);
    if (!*rect)
        return;
    wlr_scene_rect_set_size(*rect, box.width < 1 ? 1 : box.width, box.height < 1 ? 1 : box.height);
    wlr_scene_rect_set_color(*rect, colour);
    wlr_scene_node_set_position(&(*rect)->node, box.x, box.y);
    wlr_scene_node_set_enabled(&(*rect)->node, true);
}

/* Four bars of `thickness` around `box`, outside it. */
static void overview_frame_set(struct wlr_scene_rect *bars[4], struct wlr_scene_tree *parent,
                               struct sh_rect box, int thickness, const float colour[4]) {
    struct sh_rect sides[4] = {
        {box.x - thickness, box.y - thickness, box.width + 2 * thickness, thickness},
        {box.x - thickness, box.y + box.height, box.width + 2 * thickness, thickness},
        {box.x - thickness, box.y, thickness, box.height},
        {box.x + box.width, box.y, thickness, box.height},
    };
    for (int i = 0; i < 4; ++i)
        overview_rect_set(&bars[i], parent, sides[i], colour);
}

static void overview_frame_hide(struct wlr_scene_rect *bars[4]) {
    for (int i = 0; i < 4; ++i) {
        if (bars[i])
            wlr_scene_node_set_enabled(&bars[i]->node, false);
    }
}

static bool overview_listable(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return false;
#endif
    return toplevel_mapped(toplevel) && !toplevel->swallowed;
}

/* Whether a window is on `workspace` of the overview's output, for the grid and the strip. */
static bool overview_on(struct sh_overview *overview, struct sh_toplevel *toplevel,
                        int workspace) {
    return overview_listable(toplevel) && !toplevel->minimized &&
           !strcmp(toplevel->output, overview->output) &&
           (toplevel->sticky || toplevel->workspace == workspace);
}

/* Lists the windows the grid shows, keeping the selection on its window when it is still
 * there. Without a filter that is the viewed workspace's windows, most recently used first; a
 * filter lists every window that matches, minimized ones and other workspaces' included. */
static void overview_collect(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    struct sh_toplevel *kept = overview->selected >= 0 && overview->selected < overview->count
                                   ? overview->windows[overview->selected]
                                   : NULL;
    struct sh_toplevel *old[OVERVIEW_MAX];
    struct sh_thumb old_thumbs[OVERVIEW_MAX];
    int old_count = overview->count;
    memcpy(old, overview->windows, sizeof(old));
    memcpy(old_thumbs, overview->thumbs, sizeof(old_thumbs));
    memset(overview->thumbs, 0, sizeof(overview->thumbs));
    overview->count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (overview->count >= OVERVIEW_MAX || !overview_listable(toplevel))
            continue;
        bool listed;
        if (overview->filter[0]) {
            const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
            char text[512];
            snprintf(text, sizeof(text), "%s %s", app_id ? app_id : "", title ? title : "");
            listed = sh_overview_matches(text, overview->filter);
        } else {
            listed = overview_on(overview, toplevel, overview->viewed);
        }
        if (listed)
            overview->windows[overview->count++] = toplevel;
    }
    // A window that stays keeps its thumbnail, so it is not copied again.
    for (int i = 0; i < overview->count; ++i) {
        for (int j = 0; j < old_count; ++j) {
            if (old[j] == overview->windows[i] && old_thumbs[j].tree) {
                overview->thumbs[i] = old_thumbs[j];
                memset(&old_thumbs[j], 0, sizeof(old_thumbs[j]));
                break;
            }
        }
    }
    for (int j = 0; j < old_count; ++j)
        overview_thumb_clear(&old_thumbs[j]);
    overview->selected = -1;
    for (int i = 0; i < overview->count; ++i) {
        if (overview->windows[i] == kept)
            overview->selected = i;
    }
    if (overview->selected < 0 && overview->count)
        overview->selected = 0;
}

/* Where each window's thumbnail rests, and the workspace strip. Only the geometry: nothing is
 * drawn here. */
static void overview_layout(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    int gap = settings->overview_gap;
    struct sh_rect area = {overview->area.x, overview->area.y, overview->area.width,
                           overview->area.height};
    int top = area.y + OVERVIEW_TOP;
    overview->strip_count = 0;
    if (settings->overview_strip && overview->workspaces > 1) {
        struct sh_rect band = {area.x + gap, top, area.width - 2 * gap,
                               area.height / 7 > 40 ? area.height / 7 : 40};
        double aspect = (double)overview->screen.width / (overview->screen.height ? overview->screen.height : 1);
        int strip_gap = gap / 2 < 8 ? 8 : gap / 2;
        if (sh_overview_strip(overview->workspaces, band, strip_gap, aspect, band.height,
                              overview->strip_cells)) {
            overview->strip_count = overview->workspaces;
            top = overview->strip_cells[0].y + overview->strip_cells[0].height + gap;
        }
    }
    struct sh_rect grid = {area.x + gap, top, area.width - 2 * gap, area.y + area.height - gap - top};
    for (int i = 0; i < overview->count; ++i) {
        struct wlr_box box = toplevel_box(overview->windows[i]);
        overview->sizes[i] = (struct sh_rect){0, 0, box.width > 0 ? box.width : 1,
                                              box.height > 0 ? box.height : 1};
        bool shown = toplevel_visible(overview->windows[i]) &&
                     !strcmp(overview->windows[i]->output, overview->output);
        overview->placed[i] = shown;
        overview->origins[i] = (struct sh_rect){box.x, box.y, overview->sizes[i].width,
                                                overview->sizes[i].height};
    }
    if (!overview->count || !sh_overview_grid(overview->sizes, overview->count, grid, gap, 1.0,
                                              overview->cells)) {
        for (int i = 0; i < overview->count; ++i)
            overview->cells[i] = (struct sh_rect){grid.x, grid.y, 1, 1};
    }
    for (int i = 0; i < overview->count; ++i) {
        if (!overview->placed[i])
            overview->origins[i] = overview->cells[i];
    }
}

static struct sh_rect overview_rect_between(struct sh_rect from, struct sh_rect to, double t) {
    return (struct sh_rect){(int)lround(from.x + (to.x - from.x) * t),
                            (int)lround(from.y + (to.y - from.y) * t),
                            (int)lround(from.width + (to.width - from.width) * t),
                            (int)lround(from.height + (to.height - from.height) * t)};
}

/* Draws the overview at the current progress: the backdrop, the strip with a copy of each
 * workspace's windows, the cards and thumbnails, and the selection. */
static void overview_render(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    if (!overview->tree)
        return;
    double t = overview->progress;
    bool settled = t >= 1;
    float colour[4];
    overview_colour(colour, 0.04f, 0.05f, 0.08f, (float)(settings->overview_dim * t));
    overview_rect_set(&overview->backdrop, overview->tree,
                      (struct sh_rect){overview->screen.x, overview->screen.y,
                                       overview->screen.width, overview->screen.height},
                      colour);
    // The strip fades in with the backdrop by staying hidden until the grid has settled.
    for (int w = 0; w < OVERVIEW_WORKSPACES; ++w) {
        bool listed = settled && w < overview->strip_count;
        if (overview->strip_back[w])
            wlr_scene_node_set_enabled(&overview->strip_back[w]->node, listed);
        if (!listed) {
            overview_frame_hide(overview->strip_mark[w]);
            continue;
        }
        struct sh_rect cell = overview->strip_cells[w];
        bool viewed = w == overview->viewed, target = w == overview->drop;
        overview_colour(colour, viewed ? 0.22f : 0.12f, viewed ? 0.25f : 0.13f,
                        viewed ? 0.31f : 0.16f, 0.95f);
        overview_rect_set(&overview->strip_back[w], overview->strip, cell, colour);
        if (target || viewed || w == overview->current) {
            if (target)
                overview_colour(colour, 0.35f, 0.85f, 0.5f, 1);
            else if (viewed)
                overview_colour(colour, 0.36f, 0.6f, 1.0f, 1);
            else
                overview_colour(colour, 0.8f, 0.8f, 0.85f, 0.5f);
            overview_frame_set(overview->strip_mark[w], overview->strip, cell, target ? 3 : 2,
                               colour);
        } else {
            overview_frame_hide(overview->strip_mark[w]);
        }
    }
    // Copies of each workspace's windows in its strip cell, at the output's proportions.
    int minis = 0;
    if (settled) {
        for (int w = 0; w < overview->strip_count; ++w) {
            struct sh_rect cell = overview->strip_cells[w];
            double scale = (double)cell.width / (overview->screen.width ? overview->screen.width : 1);
            struct sh_toplevel *toplevel;
            wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
                if (minis >= OVERVIEW_MINI_MAX)
                    break;
                if (!overview_on(overview, toplevel, w) || toplevel == overview->dragged)
                    continue;
                struct wlr_box box = toplevel_box(toplevel);
                overview_thumb_place(&overview->minis[minis++], overview->strip, toplevel,
                                     cell.x + (int)lround((box.x - overview->screen.x) * scale),
                                     cell.y + (int)lround((box.y - overview->screen.y) * scale),
                                     scale);
            }
        }
    }
    for (int i = minis; i < OVERVIEW_MINI_MAX; ++i)
        overview_thumb_clear(&overview->minis[i]);
    overview->mini_count = minis;

    for (int i = 0; i < OVERVIEW_MAX; ++i) {
        if (i >= overview->count || !overview->windows[i]) {
            if (overview->cards[i])
                wlr_scene_node_set_enabled(&overview->cards[i]->node, false);
            continue;
        }
        struct sh_rect rect = overview_rect_between(overview->origins[i], overview->cells[i], t);
        double scale = (double)rect.width / overview->sizes[i].width;
        bool dragged = overview->dragging && overview->press == i;
        if (dragged) {
            rect.x += (int)lround(server->cursor->x - overview->press_x);
            rect.y += (int)lround(server->cursor->y - overview->press_y);
        }
        struct sh_rect card = {rect.x - OVERVIEW_PAD, rect.y - OVERVIEW_PAD,
                               rect.width + 2 * OVERVIEW_PAD, rect.height + 2 * OVERVIEW_PAD};
        overview_colour(colour, 0.1f, 0.11f, 0.14f, 0.85f);
        if (settled && !dragged)
            overview_rect_set(&overview->cards[i], overview->cards_tree, card, colour);
        else if (overview->cards[i])
            wlr_scene_node_set_enabled(&overview->cards[i]->node, false);
        overview_thumb_place(&overview->thumbs[i], overview->grid, overview->windows[i], rect.x,
                             rect.y, scale);
        if (dragged && overview->thumbs[i].tree)
            wlr_scene_node_raise_to_top(&overview->thumbs[i].tree->node);
    }
    if (settled && overview->selected >= 0 && overview->selected < overview->count &&
        !overview->dragging) {
        struct sh_rect cell = overview->cells[overview->selected];
        overview_colour(colour, 0.36f, 0.6f, 1.0f, 1);
        overview_frame_set(overview->frame, overview->frames,
                           (struct sh_rect){cell.x - OVERVIEW_PAD, cell.y - OVERVIEW_PAD,
                                            cell.width + 2 * OVERVIEW_PAD,
                                            cell.height + 2 * OVERVIEW_PAD},
                           3, colour);
    } else {
        overview_frame_hide(overview->frame);
    }
}

/* Tells the shell what to draw its text over: the output, the filter, the selection, then a
 * line per thumbnail and per strip cell, in coordinates of the output.
 *   overview OUTPUT COUNT SELECTED VIEWED STRIP AREA_X AREA_Y AREA_WIDTH AREA_HEIGHT FILTER
 *     (AREA is what the panels leave, FILTER is "-" when empty)
 *   overview-window X Y WIDTH HEIGHT APP_ID\tTITLE\tWORKSPACE\tURGENT (URGENT is 0 or 1)
 *   overview-strip X Y WIDTH HEIGHT WORKSPACE\tWINDOWS
 * and "overview-select N" as the selection moves, "overview-close" when it closes. */
size_t overview_describe(struct sh_server *server, char *text, size_t size) {
    struct sh_overview *overview = &server->overview;
    size_t length = 0;
    length += snprintf(text + length, size - length, "overview %s %d %d %d %d %d %d %d %d %s\n",
                       overview->output, overview->count, overview->selected,
                       overview->viewed + 1, overview->strip_count,
                       overview->area.x - overview->screen.x,
                       overview->area.y - overview->screen.y, overview->area.width,
                       overview->area.height, overview->filter[0] ? overview->filter : "-");
    for (int i = 0; i < overview->count && length < size; ++i) {
        struct sh_toplevel *toplevel = overview->windows[i];
        if (!toplevel)
            continue;
        const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
        char clean_title[160], clean_app_id[128];
        snprintf(clean_title, sizeof(clean_title), "%s", title ? title : "");
        snprintf(clean_app_id, sizeof(clean_app_id), "%s", app_id ? app_id : "");
        // Neither can end the line or the column: a client sets both as it likes.
        for (char *c = clean_title; *c; ++c) {
            if (*c == '\t' || *c == '\n' || *c == '\r')
                *c = ' ';
        }
        for (char *c = clean_app_id; *c; ++c) {
            if (*c == '\t' || *c == '\n' || *c == '\r')
                *c = ' ';
        }
        struct sh_rect cell = overview->cells[i];
        length += snprintf(text + length, size - length, "overview-window %d %d %d %d %s\t%s\t%d\t%d\n",
                           cell.x - overview->screen.x, cell.y - overview->screen.y, cell.width,
                           cell.height, clean_app_id, clean_title, toplevel->workspace + 1,
                           toplevel->urgent);
    }
    for (int w = 0; w < overview->strip_count && length < size; ++w) {
        struct sh_rect cell = overview->strip_cells[w];
        int windows = 0;
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) windows += overview_on(overview, toplevel, w);
        length += snprintf(text + length, size - length, "overview-strip %d %d %d %d %d\t%d\n",
                           cell.x - overview->screen.x, cell.y - overview->screen.y, cell.width,
                           cell.height, w + 1, windows);
    }
    return length < size ? length : size - 1;
}

static void overview_announce(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (wl_list_empty(&server->subscribers))
        return;
    size_t size = 512 + (size_t)(overview->count + OVERVIEW_WORKSPACES) * 512;
    char *text = malloc(size);
    if (!text)
        return;
    size_t length = overview_describe(server, text, size);
    send_event(server, text, length);
    free(text);
}

void overview_select(struct sh_server *server, int index) {
    struct sh_overview *overview = &server->overview;
    if (!overview->count || index < 0 || index >= overview->count || index == overview->selected)
        return;
    overview->selected = index;
    char line[32];
    int length = snprintf(line, sizeof(line), "overview-select %d\n", index);
    send_event(server, line, (size_t)length);
    overview_render(server);
}

/* Lays out again if the windows or workspaces changed, then draws; synchronous, so what a
 * control request changed is in place when it is answered. */
static void overview_refresh(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    if (overview->open && overview->dirty) {
        overview->dirty = false;
        overview_collect(server);
        overview_layout(server);
        overview_announce(server);
    }
    overview_render(server);
}

static void overview_hide(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    overview->visible = false;
    overview->open = false;
    overview->closing = false;
    overview->dragging = false;
    overview->press = -1;
    overview->dragged = NULL;
    for (int i = 0; i < OVERVIEW_MAX; ++i)
        overview_thumb_clear(&overview->thumbs[i]);
    for (int i = 0; i < OVERVIEW_MINI_MAX; ++i)
        overview_thumb_clear(&overview->minis[i]);
    overview->count = overview->mini_count = 0;
    if (overview->tree)
        wlr_scene_node_set_enabled(&overview->tree->node, false);
}

static int overview_step(void *data) {
    struct sh_server *server = data;
    struct sh_overview *overview = &server->overview;
    overview->armed = false;
    if (!overview->visible)
        return 0;
    if (overview->progress != overview->to) {
        int64_t elapsed = now_ms() - overview->started;
        if (overview->span <= 0 || elapsed >= overview->span) {
            overview->progress = overview->to;
        } else {
            struct sh_curve ease = {SH_CURVE_EASE_OUT, {0, 0, 0, 0}};
            overview->progress = overview->from + (overview->to - overview->from) *
                                                      sh_curve_eval(&ease, (double)elapsed /
                                                                               overview->span);
        }
    }
    overview_refresh(server);
    if (overview->progress != overview->to) {
        overview->armed = true;
        wl_event_source_timer_update(overview->timer, 16);
    } else if (overview->closing) {
        overview_hide(server);
    }
    return 0;
}

/* Asks for a redraw soon; many changes in a row make one. */
void overview_touch(struct sh_server *server, bool relayout) {
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    if (relayout)
        overview->dirty = true;
    if (!overview->armed && overview->timer) {
        overview->armed = true;
        wl_event_source_timer_update(overview->timer, 8);
    }
}

/* A window that goes away leaves the overview at once; the next layout drops its place. */
void overview_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    for (int i = 0; i < OVERVIEW_MAX; ++i) {
        if (overview->thumbs[i].toplevel == toplevel)
            overview_thumb_clear(&overview->thumbs[i]);
        if (i < overview->count && overview->windows[i] == toplevel)
            overview->windows[i] = NULL;
    }
    for (int i = 0; i < OVERVIEW_MINI_MAX; ++i) {
        if (overview->minis[i].toplevel == toplevel)
            overview_thumb_clear(&overview->minis[i]);
    }
    if (overview->dragged == toplevel) {
        overview->dragged = NULL;
        overview->dragging = false;
        overview->press = -1;
    }
    overview_touch(server, true);
}

/* Fullscreen windows sit above the other windows; while the overview is open the
 * focused one goes down among the others, as it does when it loses focus. */
static void overview_lower_fullscreen(struct sh_server *server, bool lower) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->fullscreen || !toplevel->scene_tree)
            continue;
        wlr_scene_node_reparent(&toplevel->scene_tree->node,
                                lower ? server->windows : fullscreen_tree(toplevel));
    }
}

static void overview_open(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    if (!settings->overview) {
        wlr_log(WLR_INFO, "Overview actions ignored: overview.enabled is false");
        return;
    }
    if (overview->open || server->locked || !server->overview_layer)
        return;
    struct wlr_output *output = focused_output(server);
    if (!output)
        return;
    if (overview->visible) // still gliding closed
        overview_hide(server);
    switcher_close(server, -1);
    snprintf(overview->output, sizeof(overview->output), "%s", output->name);
    wlr_output_layout_get_box(server->output_layout, output, &overview->screen);
    struct sh_rect usable = usable_area(server, output);
    overview->area = (struct sh_rect){usable.x, usable.y, usable.width, usable.height};
    overview->workspaces = settings->workspaces < OVERVIEW_WORKSPACES ? settings->workspaces
                                                                      : OVERVIEW_WORKSPACES;
    overview->current = overview->viewed = *output_workspace(server, output->name);
    overview->filter[0] = '\0';
    overview->count = 0;
    overview->selected = -1;
    overview->press = overview->drop = -1;
    overview->dragging = false;
    overview->dragged = NULL;
    overview->scroll = 0;
    if (!overview->tree) {
        overview->tree = wlr_scene_tree_create(server->overview_layer);
        if (!overview->tree)
            return;
        float clear[4] = {0, 0, 0, 0};
        overview->backdrop = wlr_scene_rect_create(overview->tree, 1, 1, clear);
        overview->strip = wlr_scene_tree_create(overview->tree);
        overview->cards_tree = wlr_scene_tree_create(overview->tree);
        overview->grid = wlr_scene_tree_create(overview->tree);
        overview->frames = wlr_scene_tree_create(overview->tree);
        overview->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                  overview_step, server);
    }
    wlr_scene_node_set_enabled(&overview->tree->node, true);
    overview_lower_fullscreen(server, true);
    overview_collect(server);
    overview->selected = -1;
    for (int i = 0; i < overview->count; ++i) {
        if (overview->windows[i] == server->focused_toplevel)
            overview->selected = i;
    }
    if (overview->selected < 0 && overview->count)
        overview->selected = 0;
    overview_layout(server);
    overview->open = overview->visible = true;
    overview->closing = false;
    overview->dirty = false;
    overview->from = overview->progress = 0;
    overview->to = 1;
    overview->span = settings->overview_animation && settings->animations
                         ? (int)(settings->overview_duration / (settings->animation_speed > 0 ? settings->animation_speed : 1))
                         : 0;
    overview->started = now_ms();
    if (overview->span <= 0)
        overview->progress = 1;
    // The pointer belongs to the overview, not to the window under it.
    wlr_seat_pointer_clear_focus(server->seat);
    set_default_cursor(server);
    overview_render(server);
    if (overview->progress != overview->to)
        overview_touch(server, false);
    overview_announce(server);
    wlr_log(WLR_INFO, "Overview opened on %s", output->name);
}

/* Closes the overview, focusing `chosen` if any; else, with `workspace` not below 0, showing
 * that workspace on the overview's output. The thumbnails glide back to the windows. */
static void overview_close(struct sh_server *server, struct sh_toplevel *chosen, int workspace) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    overview->open = false;
    overview->dragging = false;
    overview->press = overview->drop = -1;
    overview->dragged = NULL;
    overview_lower_fullscreen(server, false);
    if (chosen && !server->locked) {
        focus_toplevel(chosen);
        struct wlr_box box = toplevel_box(chosen);
        if (!wlr_box_contains_point(&box, server->cursor->x, server->cursor->y))
            pointer_follow(chosen);
    } else if (workspace >= 0) {
        struct wlr_output *output = find_output(server, overview->output);
        if (output) {
            switch_workspace(server, output, workspace);
            focus_top_on(server, output);
        }
    }
    // Where the windows are now: the thumbnails glide there.
    for (int i = 0; i < overview->count; ++i) {
        if (!overview->windows[i])
            continue;
        bool shown = toplevel_visible(overview->windows[i]) && overview_listable(overview->windows[i]);
        struct wlr_box box = toplevel_box(overview->windows[i]);
        overview->origins[i] = shown ? (struct sh_rect){box.x, box.y, overview->sizes[i].width,
                                                         overview->sizes[i].height}
                                     : overview->cells[i];
    }
    const struct sh_settings *settings = server_settings(server);
    overview->closing = true;
    overview->from = overview->progress;
    overview->to = 0;
    overview->started = now_ms();
    overview->span = settings->overview_animation && settings->animations
                         ? (int)(settings->overview_duration * overview->progress / (settings->animation_speed > 0 ? settings->animation_speed : 1))
                         : 0;
    send_event(server, "overview-close\n", strlen("overview-close\n"));
    if (overview->span <= 0) {
        overview_hide(server);
    } else {
        overview_render(server);
        overview_touch(server, false);
    }
    wlr_log(WLR_INFO, "Overview closed");
}

/* Closes at once, without the glide: the session locks, or the output goes. */
void overview_dismiss(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (overview->open)
        overview_close(server, NULL, -1);
    if (overview->visible)
        overview_hide(server);
}

static void overview_confirm(struct sh_server *server, int index) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    if (index >= 0 && index < overview->count && overview->windows[index])
        overview_close(server, overview->windows[index], -1);
    else if (!overview->count)
        overview_close(server, NULL, overview->viewed); // an empty workspace: go there
}

/* Shows another workspace of the output in the grid, without switching to it. */
void overview_view(struct sh_server *server, int workspace) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open || workspace < 0 || workspace >= overview->workspaces ||
        (workspace == overview->viewed && !overview->filter[0]))
        return;
    overview->viewed = workspace;
    overview->filter[0] = '\0';
    overview->selected = -1;
    overview->dirty = true;
    overview_refresh(server);
}

void overview_set_filter(struct sh_server *server, const char *text) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open || !strcmp(overview->filter, text))
        return;
    snprintf(overview->filter, sizeof(overview->filter), "%s", text);
    overview->selected = -1; // the first match
    overview->dirty = true;
    overview_refresh(server);
}

static void overview_close_selected(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (overview->selected >= 0 && overview->selected < overview->count &&
        overview->windows[overview->selected])
        toplevel_close(overview->windows[overview->selected]);
}

/* Index of the thumbnail (its card) under a point, else -1. */
static int overview_thumb_at(struct sh_overview *overview, double x, double y) {
    for (int i = overview->count - 1; i >= 0; --i) {
        struct sh_rect cell = overview->cells[i];
        if (x >= cell.x - OVERVIEW_PAD && x < cell.x + cell.width + OVERVIEW_PAD &&
            y >= cell.y - OVERVIEW_PAD && y < cell.y + cell.height + OVERVIEW_PAD)
            return i;
    }
    return -1;
}

static int overview_strip_at(struct sh_overview *overview, double x, double y) {
    for (int w = 0; w < overview->strip_count; ++w) {
        struct sh_rect cell = overview->strip_cells[w];
        if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height)
            return w;
    }
    return -1;
}

/* Keys while the overview is open. Text goes to the filter; the rest is navigation. Every key
 * is kept from the windows. */
static void overview_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    struct sh_overview *overview = &server->overview;
    bool control = modifiers & WLR_MODIFIER_CTRL;
    int selected = overview->selected;
    switch (sym) {
    case XKB_KEY_Escape:
        if (overview->filter[0])
            overview_set_filter(server, "");
        else
            overview_close(server, NULL, -1);
        return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        overview_confirm(server, selected);
        return;
    case XKB_KEY_Left:
    case XKB_KEY_Right:
        if (control) {
            overview_view(server, overview->viewed + (sym == XKB_KEY_Left ? -1 : 1));
            return;
        }
        // fall through
    case XKB_KEY_Up:
    case XKB_KEY_Down: {
        if (selected < 0 || !overview->count)
            return;
        enum sh_overview_direction direction = sym == XKB_KEY_Left    ? SH_OVERVIEW_LEFT
                                               : sym == XKB_KEY_Right ? SH_OVERVIEW_RIGHT
                                               : sym == XKB_KEY_Up    ? SH_OVERVIEW_UP
                                                                      : SH_OVERVIEW_DOWN;
        overview_select(server, sh_overview_neighbour(overview->cells, overview->count, selected,
                                                      direction));
        return;
    }
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: {
        if (!overview->count)
            return;
        bool back = sym == XKB_KEY_ISO_Left_Tab || (modifiers & WLR_MODIFIER_SHIFT);
        int next = (selected + (back ? -1 : 1) + overview->count) % overview->count;
        overview_select(server, next);
        return;
    }
    case XKB_KEY_Home:
        overview_select(server, 0);
        return;
    case XKB_KEY_End:
        overview_select(server, overview->count - 1);
        return;
    case XKB_KEY_Page_Up:
        overview_view(server, overview->viewed - 1);
        return;
    case XKB_KEY_Page_Down:
        overview_view(server, overview->viewed + 1);
        return;
    case XKB_KEY_Delete:
        overview_close_selected(server);
        return;
    case XKB_KEY_BackSpace: {
        size_t length = strlen(overview->filter);
        if (!length)
            return;
        char text[sizeof(overview->filter)];
        memcpy(text, overview->filter, length);
        while (length > 0 && (text[length - 1] & 0xC0) == 0x80)
            --length; // step back over a multibyte character's tail
        if (length > 0)
            --length;
        text[length] = '\0';
        overview_set_filter(server, text);
        return;
    }
    }
    if (modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        return;
    char utf8[8];
    int bytes = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
    if (bytes <= 1 || (unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f)
        return; // bytes counts the terminator: 1 means no character
    char text[sizeof(overview->filter)];
    size_t length = strlen(overview->filter);
    if (length + (size_t)bytes >= sizeof(text))
        return;
    memcpy(text, overview->filter, length);
    memcpy(text + length, utf8, (size_t)bytes);
    overview_set_filter(server, text);
}

bool overview_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    struct sh_overview *overview = &server->overview;
    double x = server->cursor->x, y = server->cursor->y;
    uint32_t bit = event->button >= BTN_MOUSE && event->button < BTN_MOUSE + 32
                       ? 1u << (event->button - BTN_MOUSE)
                       : 0;
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(overview->pressed & bit))
            return false;
        overview->pressed &= ~bit;
        if (event->button != BTN_LEFT || overview->press < 0)
            return true;
        int press = overview->press;
        bool dragging = overview->dragging;
        int drop = overview->drop;
        struct sh_toplevel *toplevel = press < overview->count ? overview->windows[press] : NULL;
        overview->press = overview->drop = -1;
        overview->dragging = false;
        overview->dragged = NULL;
        if (!overview->open)
            return true;
        if (dragging) {
            if (toplevel && drop >= 0 && drop != toplevel->workspace &&
                !strcmp(toplevel->output, overview->output)) {
                move_toplevel_to_workspace(server, toplevel, drop);
                overview->dirty = true;
            }
            overview_refresh(server);
        } else if (overview_thumb_at(overview, x, y) == press) {
            overview_confirm(server, press);
        }
        return true;
    }
    if (!overview->open)
        return false;
    // A click on a panel, or on another monitor, closes the overview and goes on to what is
    // there: the taskbar's buttons still work.
    struct wlr_box area = {overview->area.x, overview->area.y, overview->area.width,
                           overview->area.height};
    if (!wlr_box_contains_point(&area, x, y)) {
        overview_close(server, NULL, -1);
        process_cursor_motion(server, event->time_msec);
        return false;
    }
    overview->pressed |= bit;
    if (event->button == BTN_LEFT) {
        int thumb = overview_thumb_at(overview, x, y), cell = overview_strip_at(overview, x, y);
        if (thumb >= 0) {
            overview_select(server, thumb);
            overview->press = thumb;
            overview->press_x = x;
            overview->press_y = y;
        } else if (cell >= 0) {
            if (cell == overview->viewed && !overview->filter[0])
                overview_close(server, NULL, cell); // a second click goes there
            else
                overview_view(server, cell);
        } else {
            overview_close(server, NULL, -1);
        }
    } else if (event->button == BTN_MIDDLE) {
        int thumb = overview_thumb_at(overview, x, y);
        if (thumb >= 0 && overview->windows[thumb])
            toplevel_close(overview->windows[thumb]);
    }
    return true;
}

bool overview_motion(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return false;
    double x = server->cursor->x, y = server->cursor->y;
    if (overview->press >= 0 && !overview->dragging &&
        fabs(x - overview->press_x) + fabs(y - overview->press_y) > 8 &&
        overview->press < overview->count) {
        overview->dragging = true;
        overview->dragged = overview->windows[overview->press];
    }
    if (overview->dragging) {
        overview->drop = overview_strip_at(overview, x, y);
        overview_render(server);
    } else if (overview->press < 0) {
        int thumb = overview_thumb_at(overview, x, y);
        if (thumb >= 0)
            overview_select(server, thumb);
    }
    set_default_cursor(server);
    wlr_seat_pointer_clear_focus(server->seat);
    return true;
}

/* The hot corner: entering the configured corner of an output opens the overview there. */
void overview_hot_corner(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    int corner = settings->overview_hot_corner;
    bool inside = false;
    struct wlr_output *output = NULL;
    if (settings->overview && corner > 0 && !server->locked &&
        server->cursor_mode == SH_CURSOR_PASSTHROUGH) {
        output = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
                                             server->cursor->y);
        if (output) {
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output, &box);
            double x = corner == 2 || corner == 4 ? box.x + box.width - 1 : box.x;
            double y = corner == 3 || corner == 4 ? box.y + box.height - 1 : box.y;
            inside = fabs(server->cursor->x - x) < 2 && fabs(server->cursor->y - y) < 2;
        }
    }
    if (inside && !overview->in_corner && !overview->open && !server->switcher.open) {
        server->target_output = output;
        overview_open(server);
        server->target_output = NULL;
    }
    overview->in_corner = inside;
}

/* The wheel pages through the workspaces. */
bool overview_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return false;
    if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return true;
    overview->scroll += event->delta;
    while (overview->scroll >= 10) {
        overview->scroll -= 10;
        overview_view(server, overview->viewed + 1);
    }
    while (overview->scroll <= -10) {
        overview->scroll += 10;
        overview_view(server, overview->viewed - 1);
    }
    return true;
}

/* Sway's scratchpad. A window put there floats and hides, listed in the taskbar as minimized.
 * scratchpad_show brings one to the middle of the focused output's current workspace, where it
 * stays in the scratchpad (and hides again on the next scratchpad_show) until it is moved to a
 * workspace or tiled. */
static bool scratchpad_enabled(struct sh_server *server) {
    if (server_settings(server)->scratchpad)
        return true;
    if (!server->scratchpad_off_logged)
        wlr_log(WLR_INFO, "Scratchpad actions ignored: features.scratchpad is false");
    server->scratchpad_off_logged = true;
    return false;
}

/* Moves a scratchpad window onto the current workspace of `output`, centred in its floating
 * area and no larger than it. */
static void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_server *server = toplevel->server;
    if (!output)
        return;
    snprintf(toplevel->output, sizeof(toplevel->output), "%s", output->name);
    toplevel->workspace = *output_workspace(server, output->name);
    struct sh_rect area = floating_area(server, output);
    struct wlr_box box = toplevel_box(toplevel);
    box.width = box.width > area.width ? area.width : box.width;
    box.height = box.height > area.height ? area.height : box.height;
    box.x = area.x + (area.width - box.width) / 2;
    box.y = area.y + (area.height - box.height) / 2;
    toplevel_configure_box(toplevel, box);
}

/* Floats the window at its own size and hides it in the scratchpad, behind the others. */
void hide_in_scratchpad(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    if (toplevel->sticky)
        set_sticky(toplevel, false, false); // a hidden window shows on no workspace
    toplevel->floating = true;
    toplevel->placed = false;
    if (toplevel->tiled)
        untile_toplevel(toplevel, true);
    else
        restore_toplevel(toplevel); // snapped or maximized
    toplevel->scratchpad = true;
    toplevel->scratchpad_order = ++server->scratchpad_serial;
    minimize_toplevel(toplevel);
    notify_subscribers(server); // its workspace may be empty now
}

/* Hides the focused scratchpad window; else focuses one shown on the focused output; else
 * shows the hidden one put there longest ago, or failing that takes one shown elsewhere. */
static void scratchpad_show(struct sh_server *server) {
    struct sh_toplevel *focused = server->focused_toplevel;
    if (focused && focused->scratchpad) {
        hide_in_scratchpad(focused);
        return;
    }
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel, *next = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->scratchpad && toplevel_visible(toplevel) &&
            find_output(server, toplevel->output) == output) {
            focus_toplevel(toplevel);
            return;
        }
    }
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        if (!next || (toplevel->minimized && !next->minimized) ||
            (toplevel->minimized == next->minimized &&
             toplevel->scratchpad_order < next->scratchpad_order))
            next = toplevel;
    }
    if (!next || !output)
        return;
    center_scratchpad(next, output);
    focus_toplevel(next);
    notify_subscribers(server);
}

/* With features.scratchpad turned off, its windows come back to the focused output. */
static void empty_scratchpad(struct sh_server *server) {
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        toplevel->scratchpad = false;
        if (!toplevel->minimized)
            continue;
        center_scratchpad(toplevel, output);
        toplevel->minimized = false;
        if (toplevel->foreign)
            wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, false);
    }
}

/* Hands the output under the pointer or the focused window's box to the configuration side, which
 * runs grim in the background. */
bool take_screenshot(struct sh_server *server, enum sh_screenshot_mode mode, char *error,
                     size_t error_size) {
    const char *output_name = NULL;
    struct sh_rect box = {0};
    if (mode == SH_SCREENSHOT_OUTPUT) {
        struct wlr_output *output = wlr_output_layout_output_at(
            server->output_layout, server->cursor->x, server->cursor->y);
        if (!output) {
            snprintf(error, error_size, "no output under the pointer");
            return false;
        }
        output_name = output->name;
    } else if (mode == SH_SCREENSHOT_WINDOW) {
        struct sh_toplevel *current = current_toplevel(server);
        if (!current) {
            snprintf(error, error_size, "no focused window");
            return false;
        }
        struct wlr_box geometry = toplevel_box(current);
        box = (struct sh_rect){geometry.x, geometry.y, geometry.width, geometry.height};
    }
    return server->callbacks->screenshot(server->callbacks->userdata, mode, output_name, &box,
                                         error, error_size);
}

/* The tiling layout actions, on the focused output's current workspace. */
static void layout_action(struct sh_server *server, enum sh_action action) {
    struct wlr_output *output = focused_output(server);
    if (!output || server->locked)
        return;
    const char *name = output->name;
    int workspace = *output_workspace(server, name);
    struct sh_toplevel *current = current_toplevel(server);
    if (current && (!current->tiled || tiled_output(current) != output ||
                    current->workspace != workspace))
        current = NULL;
    bool changed = true;
    // The master keys resize the focused column in the scrolling layout.
    if (sh_tiling_layout(server->tiling, name, workspace) == SH_LAYOUT_SCROLL) {
        if (action == SH_MASTER_GROW)
            action = SH_COLUMN_WIDEN;
        else if (action == SH_MASTER_SHRINK)
            action = SH_COLUMN_NARROW;
    }
    switch (action) {
    case SH_LAYOUT_NEXT:
    case SH_LAYOUT_PREV:
        sh_tiling_cycle_layout(server->tiling, name, workspace, action == SH_LAYOUT_NEXT ? 1 : -1);
        break;
    case SH_SET_LAYOUT_DWINDLE:
    case SH_SET_LAYOUT_MASTER:
    case SH_SET_LAYOUT_SPIRAL:
    case SH_SET_LAYOUT_MONOCLE:
    case SH_SET_LAYOUT_SCROLL:
        sh_tiling_set_layout(server->tiling, name, workspace,
                             (enum sh_tile_layout)(action - SH_SET_LAYOUT_DWINDLE));
        break;
    case SH_PROMOTE: {
        void *master = sh_tiling_master(server->tiling, name, workspace);
        if (current && master == current)
            master = sh_tiling_neighbour(server->tiling, current, 1);
        changed = current && master && sh_tiling_swap(server->tiling, current, master);
        break;
    }
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV: {
        void *other = current ? sh_tiling_neighbour(server->tiling, current,
                                                     action == SH_SWAP_NEXT ? 1 : -1)
                              : NULL;
        changed = other && sh_tiling_swap(server->tiling, current, other);
        break;
    }
    case SH_FOCUS_NEXT:
    case SH_FOCUS_PREV: {
        struct sh_toplevel *other = current ? sh_tiling_neighbour(server->tiling, current,
                                                                  action == SH_FOCUS_NEXT ? 1 : -1)
                                            : NULL;
        if (other) {
            focus_toplevel(other);
            pointer_follow(other);
        }
        return;
    }
    case SH_MASTER_GROW:
    case SH_MASTER_SHRINK:
        changed = sh_tiling_adjust(server->tiling, name, workspace,
                                   action == SH_MASTER_GROW ? 0.05 : -0.05, 0);
        break;
    case SH_MASTER_MORE:
    case SH_MASTER_LESS:
        changed = sh_tiling_adjust(server->tiling, name, workspace, 0,
                                   action == SH_MASTER_MORE ? 1 : -1);
        break;
    case SH_SCROLL_LEFT:
    case SH_SCROLL_RIGHT: {
        struct sh_toplevel *other = current ? sh_tiling_scroll_step(server->tiling, current,
                                                                    action == SH_SCROLL_RIGHT ? 1 : -1, 0)
                                            : NULL;
        if (other) {
            focus_toplevel(other);
            pointer_follow(other);
        }
        return;
    }
    case SH_COLUMN_WIDEN:
    case SH_COLUMN_NARROW:
    case SH_COLUMN_CYCLE_WIDTH:
    case SH_CONSUME_LEFT:
    case SH_CONSUME_RIGHT:
    case SH_EXPEL:
    case SH_CENTER_COLUMN:
        changed = current && sh_tiling_scroll_action(server->tiling, current, action);
        break;
    default:
        return;
    }
    if (!changed)
        return;
    if (current)
        sh_tiling_set_focus(server->tiling, current); // a new layout finds the view to move
    reflow_output(server, output);
    // Monocle stacks the tiles: keep the focused one on top.
    if (current)
        focus_toplevel(current);
}

/* Shared by key bindings and the control socket. */
void run_action(struct sh_server *server, enum sh_action action, int argument) {
    int count = server_settings(server)->workspaces;
    struct sh_toplevel *current = current_toplevel(server);
    switch (action) {
    case SH_NONE:
    case SH_HANDLED:
        break;
    case SH_QUIT:
        wl_display_terminate(server->wl_display);
        break;
    case SH_RELOAD:
        reload_config(server);
        break;
    case SH_CYCLE: {
        // Raise the least recently focused visible window.
        struct sh_toplevel *toplevel;
        wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
            if (toplevel != current && toplevel_visible(toplevel)) {
                focus_toplevel(toplevel);
                break;
            }
        }
        break;
    }
    case SH_FOCUS_LAST:
        focus_last(server);
        break;
    case SH_FOCUS_URGENT:
        focus_urgent(server);
        break;
    case SH_GROUP_TOGGLE:
        if (groups_enabled(server))
            group_toggle(server, current);
        break;
    case SH_GROUP_NEXT:
    case SH_GROUP_PREV:
        if (groups_enabled(server))
            group_cycle(server, current, action == SH_GROUP_NEXT ? 1 : -1);
        break;
    case SH_UNGROUP:
        if (groups_enabled(server))
            ungroup(server, current);
        break;
    case SH_GROUP_MERGE_LEFT:
    case SH_GROUP_MERGE_RIGHT:
    case SH_GROUP_MERGE_UP:
    case SH_GROUP_MERGE_DOWN:
        if (groups_enabled(server))
            group_merge(server, action);
        break;
    case SH_FULLSCREEN:
        if (current)
            set_fullscreen(current, !current->fullscreen);
        break;
    case SH_CLOSE:
        if (current)
            toplevel_close(current);
        break;
    case SH_WORKSPACE: {
        struct wlr_output *output = focused_output(server);
        int workspace = argument - 1;
        // With back-and-forth, naming the workspace already shown returns to the previous one.
        if (output && server_settings(server)->workspace_back_and_forth &&
            workspace == *output_workspace(server, output->name))
            workspace = server->output_workspaces[output_slot(server, output->name)].previous;
        switch_workspace(server, output, workspace);
        break;
    }
    case SH_WORKSPACE_BACK: {
        struct wlr_output *output = focused_output(server);
        if (output)
            switch_workspace(server, output,
                             server->output_workspaces[output_slot(server, output->name)].previous);
        break;
    }
    case SH_MOVE_TO_WORKSPACE:
        move_to_workspace(server, argument - 1);
        break;
    case SH_WORKSPACE_NEXT:
    case SH_WORKSPACE_PREV: {
        struct wlr_output *output = focused_output(server);
        if (!output)
            break;
        int step = action == SH_WORKSPACE_NEXT ? 1 : count - 1;
        switch_workspace(server, output, (*output_workspace(server, output->name) + step) % count);
        break;
    }
    case SH_TOGGLE_TILING: {
        struct wlr_output *output = focused_output(server);
        set_tiling(server, output, !output_tiles(server, output));
        break;
    }
    case SH_LAYOUT_NEXT:
    case SH_LAYOUT_PREV:
    case SH_SET_LAYOUT_DWINDLE:
    case SH_SET_LAYOUT_MASTER:
    case SH_SET_LAYOUT_SPIRAL:
    case SH_SET_LAYOUT_MONOCLE:
    case SH_SET_LAYOUT_SCROLL:
    case SH_SCROLL_LEFT:
    case SH_SCROLL_RIGHT:
    case SH_COLUMN_WIDEN:
    case SH_COLUMN_NARROW:
    case SH_COLUMN_CYCLE_WIDTH:
    case SH_CONSUME_LEFT:
    case SH_CONSUME_RIGHT:
    case SH_EXPEL:
    case SH_CENTER_COLUMN:
    case SH_PROMOTE:
    case SH_FOCUS_NEXT:
    case SH_FOCUS_PREV:
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV:
    case SH_MASTER_GROW:
    case SH_MASTER_SHRINK:
    case SH_MASTER_MORE:
    case SH_MASTER_LESS:
        layout_action(server, action);
        break;
    case SH_LAUNCHER:
        request_launcher(server);
        break;
    case SH_PALETTE:
        request_palette(server);
        break;
    case SH_FOCUS_LEFT:
    case SH_FOCUS_RIGHT:
    case SH_FOCUS_UP:
    case SH_FOCUS_DOWN:
        focus_direction(server, action);
        break;
    case SH_MOVE_LEFT:
    case SH_MOVE_RIGHT:
    case SH_MOVE_UP:
    case SH_MOVE_DOWN:
        move_window(server, action);
        break;
    case SH_MOVE_TO_SCRATCHPAD:
        if (current && scratchpad_enabled(server))
            hide_in_scratchpad(current);
        break;
    case SH_SCRATCHPAD_SHOW:
        if (scratchpad_enabled(server))
            scratchpad_show(server);
        break;
    case SH_RESIZE_LEFT:
    case SH_RESIZE_RIGHT:
    case SH_RESIZE_UP:
    case SH_RESIZE_DOWN:
        resize_window(server, action, argument);
        break;
    case SH_SCREENSHOT: {
        char error[256] = "";
        if (!take_screenshot(server, (enum sh_screenshot_mode)argument, error, sizeof(error)))
            wlr_log(WLR_ERROR, "Screenshot not taken: %s", error);
        break;
    }
    case SH_SWITCHER_NEXT:
    case SH_SWITCHER_PREV:
        switcher_open(server, action == SH_SWITCHER_PREV, 0, XKB_KEY_NoSymbol);
        break;
    case SH_SWITCHER_CONFIRM:
        switcher_close(server, argument > 0 ? argument - 1 : server->switcher.selected);
        break;
    case SH_SWITCHER_CANCEL:
        switcher_close(server, -1);
        break;
    case SH_OVERVIEW_TOGGLE:
        if (server->overview.open)
            overview_close(server, NULL, -1);
        else
            overview_open(server);
        break;
    case SH_OVERVIEW_CONFIRM:
        overview_confirm(server, argument > 0 ? argument - 1 : server->overview.selected);
        break;
    case SH_OVERVIEW_CANCEL:
        overview_close(server, NULL, -1);
        break;
    case SH_PEEK:
    case SH_PEEK_TOGGLE:
        set_peek(server, !server->peeking);
        break;
    case SH_NIGHT_LIGHT_TOGGLE:
        server->night_mode = server->night_kelvin < SH_KELVIN_NEUTRAL ? SH_NIGHT_OFF : SH_NIGHT_ON;
        night_light_update(server);
        break;
    case SH_NIGHT_LIGHT_ON:
        server->night_mode = SH_NIGHT_ON;
        night_light_update(server);
        break;
    case SH_NIGHT_LIGHT_OFF:
        server->night_mode = SH_NIGHT_OFF;
        night_light_update(server);
        break;
    case SH_NIGHT_LIGHT_AUTO:
        server->night_mode = SH_NIGHT_AUTO;
        night_light_update(server);
        break;
    case SH_ZOOM_IN:
        zoom_by(server, 1);
        break;
    case SH_ZOOM_OUT:
        zoom_by(server, -1);
        break;
    case SH_ZOOM_RESET:
        zoom_by(server, 0);
        break;
    case SH_SWALLOW_TOGGLE:
        swallow_toggle(server, current);
        break;
    case SH_MOVE_WORKSPACE_TO_OUTPUT:
    case SH_SWAP_WORKSPACES: {
        const char *target = server->callbacks->action_target
                                 ? server->callbacks->action_target(server->callbacks->userdata)
                                 : "";
        if (action == SH_MOVE_WORKSPACE_TO_OUTPUT)
            move_workspace_to_output(server, target);
        else
            swap_output_workspaces(server, target);
        break;
    }
    case SH_DND_TOGGLE:
        send_shell_line(server, "dnd toggle\n");
        break;
    case SH_DND_ON:
        send_shell_line(server, "dnd on\n");
        break;
    case SH_DND_OFF:
        send_shell_line(server, "dnd off\n");
        break;
    case SH_NOTIFICATION_HISTORY:
        request_shell(server, "notifications");
        break;
    case SH_TOGGLE_STICKY:
        if (current && server_settings(server)->sticky)
            set_sticky(current, !current->sticky, true);
        break;
    case SH_TOGGLE_FLOATING:
        if (current && current->sticky) {
            current->sticky_floating = false; // it tiles once it is no longer sticky
            set_sticky(current, false, true);
        } else if (current && current->tiled) {
            current->floating = true;
            current->placed = false;
            untile_toplevel(current, true);
        } else if (current) {
            current->floating = false;
            current->scratchpad = false; // tiled, it leaves the scratchpad
            if (wants_tiling(current, NULL))
                tile_toplevel(current, NULL, NULL, true);
        }
        break;
    default:
        arrange_windows(server, action);
        break;
    }
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

static bool configure_keyboard(struct sh_server *server, struct wlr_keyboard *keyboard) {
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
static void configure_pointer(struct sh_server *server, struct wlr_input_device *device) {
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

static void server_new_input(struct wl_listener *listener, void *data) {
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
static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    server_new_input(&server->new_input, &keyboard->keyboard.base);
}

static void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    struct wlr_input_device *device = &event->new_pointer->pointer.base;
    server_new_input(&server->new_input, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(server->cursor, device, event->suggested_output);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_cursor);

    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        server->shape_edges = 0;
        server->shown_edges = 0;
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void set_default_cursor(struct sh_server *server) {
    server->shape_edges = 0;
    server->shown_edges = 0;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pointer_focus_change);
    struct wlr_seat_pointer_focus_change_event *event = data;
    if (!event->new_surface)
        set_default_cursor(server);
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

/* Drag-and-drop (browser tabs, files into chat windows): only from a real button press. */
static void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;
    if (!server->locked && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

static void seat_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, start_drag);
    struct wlr_drag *drag = data;
    wlr_scene_node_set_position(&server->drag_icons->node, server->cursor->x, server->cursor->y);
    // The scene helper removes the icon's node when the icon goes away.
    if (drag->icon)
        wlr_scene_drag_icon_create(server->drag_icons, drag->icon);
}

static struct sh_toplevel *toplevel_for_surface(struct sh_server *server,
                                                struct wlr_surface *surface) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) == surface)
            return toplevel;
    }
    return NULL;
}

/* xdg-activation: an application asks to be raised, e.g. a browser opening a link from chat.
 * wlroots expires and validates tokens. Tokens made without an input serial are honoured too:
 * a browser handed a link by another process often has nothing better. What the window then
 * gets depends on windows.activation. */
static void request_activate(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    struct sh_toplevel *toplevel = toplevel_for_surface(server, event->surface);
    if (toplevel)
        activation_requested(toplevel);
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

static void server_new_constraint(struct wl_listener *listener, void *data) {
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

static void seat_keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, keyboard_focus_change);
    struct wlr_seat_keyboard_focus_change_event *event = data;
    set_active_constraint(server, event->new_surface
                                      ? wlr_pointer_constraints_v1_constraint_for_surface(
                                            server->constraints, event->new_surface, server->seat)
                                      : NULL);
}

static void configure_animations(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_animator_config config = {.enabled = settings->animations,
                                        .speed = settings->animation_speed,
                                        .late_ms = settings->animation_late_ms};
    memcpy(config.styles, settings->animation_styles, sizeof(config.styles));
    sh_animator_configure(server->animator, &config);
}

static void reload_config(struct sh_server *server) {
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
