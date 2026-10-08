/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1): a virtual machine, a
 * remote desktop or a game asks for the keys the compositor's bindings take, which then go to its
 * surface while it has the keyboard. toggle_shortcuts_inhibit turns the focused one off and on
 * again, its binding the one that still runs; keyboard.shortcuts_inhibit and a window rule's
 * shortcuts_inhibit = false refuse them. */
#include "server.h"

struct sh_shortcuts_inhibitor {
    struct wl_list link; // sh_server.shortcuts.inhibitors
    struct sh_server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *wlr_inhibitor;
    struct wlr_surface *surface;
    /* Refused by keyboard.shortcuts_inhibit or a window rule, as last worked out; turned off by
     * the user (toggle_shortcuts_inhibit) until they turn it on again. */
    bool refused, off;
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

/* Honours the inhibitor, or stops honouring it, as the settings and the user say; its client
 * hears of each change (active, inactive). */
static void apply(struct sh_shortcuts_inhibitor *inhibitor) {
    inhibitor->refused = refused(inhibitor);
    bool active = !inhibitor->refused && !inhibitor->off;
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

/* The name the user knows the surface's window by, its title, else its app id, as a piece of a
 * line to the shell: at most `size` - 1 bytes, cut between characters, on one line. */
static void surface_name(struct sh_server *server, struct wlr_surface *surface, char *name,
                         size_t size) {
    struct sh_toplevel *toplevel = surface_toplevel(server, surface);
    const char *title = toplevel ? toplevel_title(toplevel) : NULL;
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    snprintf(name, size, "%s", title && *title ? title : app_id && *app_id ? app_id : "the window");
    // A character snprintf cut short at the end goes.
    size_t length = strlen(name), start = length;
    while (start > 0 && ((unsigned char)name[start - 1] & 0xC0) == 0x80)
        --start;
    unsigned char lead = start > 0 ? (unsigned char)name[start - 1] : 0;
    if (length - start < (lead >= 0xF0 ? 3u : lead >= 0xE0 ? 2u : lead >= 0xC0 ? 1u : 0u))
        name[start - 1] = '\0';
    for (char *c = name; *c; ++c)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
}

/* toggle_shortcuts_inhibit: the focused surface's inhibitor turned off, which gives the bindings
 * their keys back, or on again, which the on-screen display says. One the settings refuse stays
 * refused. */
void toggle_shortcuts_inhibit(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    if (!inhibitor) {
        wlr_log(WLR_INFO, "The focused surface asks for no shortcuts");
        return;
    }
    apply(inhibitor);
    if (inhibitor->refused) {
        wlr_log(WLR_INFO, "The focused surface's shortcuts inhibitor is refused "
                          "(keyboard.shortcuts_inhibit or a window rule)");
        return;
    }
    inhibitor->off = !inhibitor->off;
    apply(inhibitor);
    shortcuts_changed(server);
    struct wlr_output *output = focused_output(server);
    char name[160], line[256];
    surface_name(server, inhibitor->surface, name, sizeof(name));
    if (inhibitor->off)
        snprintf(line, sizeof(line), "osd %s -1 Shortcuts back to the desktop\n",
                 output ? output->name : "-");
    else
        snprintf(line, sizeof(line), "osd %s -1 Shortcuts go to %s\n", output ? output->name : "-",
                 name);
    send_shell_line(server, line);
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
 * line per inhibitor: "inhibitor", whether it is honoured ("active"), turned off by the user
 * ("off") or refused ("refused"), whether its surface has the keyboard, and the surface as
 * `surface` describes it. */
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
                 inhibitor->wlr_inhibitor->active ? "active"
                 : inhibitor->refused             ? "refused"
                                                  : "off",
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
