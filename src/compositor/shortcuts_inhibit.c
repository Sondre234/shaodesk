/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1): a virtual machine, a
 * remote desktop or a game asks for the keys the compositor's bindings take, which then go to its
 * surface while it has the keyboard. keyboard.shortcuts_inhibit and a window rule's
 * shortcuts_inhibit = false refuse them. */
#include "server.h"

struct sh_shortcuts_inhibitor {
    struct wl_list link; // sh_server.shortcuts.inhibitors
    struct sh_server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *wlr_inhibitor;
    struct wlr_surface *surface;
    /* Refused by keyboard.shortcuts_inhibit or a window rule, as last worked out. */
    bool refused;
    struct wl_listener destroy;
};

/* The window a surface belongs to, or NULL for a panel, a lock surface and the like. */
static struct sh_toplevel *surface_toplevel(struct sh_server *server, struct wlr_surface *surface) {
    struct wlr_surface *root = wlr_surface_get_root_surface(surface);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) == root)
            return toplevel;
    }
    return NULL;
}

/* Whether the settings refuse the inhibitor: keyboard.shortcuts_inhibit, or the window rules for
 * its window as it is now called. */
static bool refused(struct sh_shortcuts_inhibitor *inhibitor) {
    struct sh_server *server = inhibitor->server;
    if (!server_settings(server)->shortcuts_inhibit)
        return true;
    struct sh_toplevel *toplevel = surface_toplevel(server, inhibitor->surface);
    if (!toplevel)
        return false;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    struct sh_window_rule rule = {.floating = -1};
    return server->callbacks->window_rule(server->callbacks->userdata, app_id ? app_id : "",
                                          title ? title : "", &rule) &&
           rule.no_shortcuts_inhibit;
}

/* Honours the inhibitor, or stops honouring it, as the settings say; its client hears of each
 * change (active, inactive). */
static void apply(struct sh_shortcuts_inhibitor *inhibitor) {
    inhibitor->refused = refused(inhibitor);
    bool active = !inhibitor->refused;
    if (active && !inhibitor->wlr_inhibitor->active)
        wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor->wlr_inhibitor);
    else if (!active && inhibitor->wlr_inhibitor->active)
        wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor->wlr_inhibitor);
}

/* The inhibitor of the surface with the keyboard, honoured or not; NULL for none. */
static struct sh_shortcuts_inhibitor *focused_inhibitor(struct sh_server *server) {
    struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->shortcuts.inhibitors, link) {
        if (focus && inhibitor->surface == focus)
            return inhibitor;
    }
    return NULL;
}

/* Whether the keys go to the focused surface rather than to the bindings: it holds an inhibitor
 * that is honoured. Never while the session is locked, where the `locked` bindings run as
 * ever. */
bool shortcuts_inhibited(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    return !server->locked && inhibitor && inhibitor->wlr_inhibitor->active;
}

/* Notes which inhibitor takes the keys now. One that has just started to leaves the binding mode
 * in use, as locking does, so that the bindings outside any mode are the ones it holds. */
static void shortcuts_changed(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *now = shortcuts_inhibited(server) ? focused_inhibitor(server)
                                                                     : NULL;
    if (now == server->shortcuts.effective)
        return;
    server->shortcuts.effective = now;
    if (!now) {
        wlr_log(WLR_INFO, "Shortcuts are the compositor's again");
        return;
    }
    struct sh_toplevel *toplevel = surface_toplevel(server, now->surface);
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    wlr_log(WLR_INFO, "Shortcuts go to %s", app_id && *app_id ? app_id : "a surface without a window");
    set_binding_mode(server, 0);
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
    struct sh_shortcuts_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct sh_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    wl_list_remove(&inhibitor->link);
    if (server->shortcuts.effective == inhibitor)
        server->shortcuts.effective = NULL;
    free(inhibitor);
    shortcuts_changed(server);
}

static void new_inhibitor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, shortcuts.new_inhibitor);
    struct wlr_keyboard_shortcuts_inhibitor_v1 *wlr_inhibitor = data;
    struct sh_shortcuts_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return; // never honoured: its client hears nothing
    inhibitor->server = server;
    inhibitor->wlr_inhibitor = wlr_inhibitor;
    inhibitor->surface = wlr_inhibitor->surface;
    add_listener(&wlr_inhibitor->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    wl_list_insert(&server->shortcuts.inhibitors, &inhibitor->link);
    apply(inhibitor);
    if (inhibitor->refused)
        wlr_log(WLR_INFO, "Refused a keyboard shortcuts inhibitor (keyboard.shortcuts_inhibit or "
                          "a window rule)");
    shortcuts_changed(server);
}

static void keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, shortcuts.keyboard_focus_change);
    // A window's rules are matched again as it gets the keyboard, by the name it has now.
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    if (inhibitor)
        apply(inhibitor);
    shortcuts_changed(server);
}

/* A reload may have changed keyboard.shortcuts_inhibit and the window rules. */
void shortcuts_reload(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->shortcuts.inhibitors, link) apply(inhibitor);
    shortcuts_changed(server);
}

/* For `get shortcuts`: "inhibited 1" while the keys go to the focused surface (else 0), then a
 * line per inhibitor: "inhibitor", whether it is honoured ("active") or not ("refused"),
 * whether its surface has the keyboard, and the surface as `surface` describes it. */
void describe_shortcuts_inhibitors(struct sh_server *server, int fd,
                                   void (*surface)(struct sh_server *, int, const char *,
                                                   struct wlr_surface *)) {
    char line[64];
    snprintf(line, sizeof(line), "inhibited\t%d\n", shortcuts_inhibited(server));
    control_reply(fd, line);
    struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each_reverse(inhibitor, &server->shortcuts.inhibitors, link) {
        snprintf(line, sizeof(line), "inhibitor\t%s\t%d",
                 inhibitor->wlr_inhibitor->active ? "active" : "refused",
                 focus && inhibitor->surface == focus);
        surface(server, fd, line, inhibitor->surface);
    }
}

void shortcuts_inhibit_init(struct sh_server *server) {
    wl_list_init(&server->shortcuts.inhibitors);
    server->shortcuts.manager = wlr_keyboard_shortcuts_inhibit_v1_create(server->wl_display);
    add_listener(&server->shortcuts.manager->events.new_inhibitor,
                 &server->shortcuts.new_inhibitor, new_inhibitor);
    add_listener(&server->seat->keyboard_state.events.focus_change,
                 &server->shortcuts.keyboard_focus_change, keyboard_focus_change);
}

void shortcuts_inhibit_finish(struct sh_server *server) {
    wl_list_remove(&server->shortcuts.new_inhibitor.link);
    wl_list_remove(&server->shortcuts.keyboard_focus_change.link);
}
