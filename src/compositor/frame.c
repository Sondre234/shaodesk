/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
#include "server.h"

/* BEGIN FORWARD */
static void fade_update(void *data);
/* END FORWARD */

/* Windows are decorated by the server (no title bar, just the window controls) unless they ask to draw
 * their own frame, as frameless Electron apps like Discord do. */
enum wlr_xdg_toplevel_decoration_v1_mode
decoration_mode(struct wlr_xdg_toplevel_decoration_v1 *decoration) {
    return decoration->requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

bool wants_decoration(struct sh_toplevel *toplevel) {
    if (toplevel->decoration)
        return toplevel->decoration->current.mode ==
               WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
#if WLR_HAS_XWAYLAND
    // X11 windows that leave decorations to the window manager (Spotify, for one).
    return toplevel->xsurface && !toplevel->unmanaged &&
           toplevel->xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
#else
    return false;
#endif
}

static struct wlr_buffer *deco_buffer(struct sh_server *server, enum sh_deco_part hovered) {
    if (!server->deco_buffers[hovered]) {
        float scale = 1;
        struct sh_output *output;
        wl_list_for_each(output, &server->outputs, link) {
            if (output->wlr_output->scale > scale)
                scale = output->wlr_output->scale;
        }
        server->deco_buffers[hovered] = sh_decoration_render((int)ceilf(scale), hovered);
    }
    return server->deco_buffers[hovered];
}

/* The controls' place inside the window's scene tree: its top-right corner. */
static void deco_position(struct sh_toplevel *toplevel, int *x, int *y) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    *x = geometry.x + geometry.width - SH_DECO_MARGIN - SH_DECO_WIDTH;
    if (*x < geometry.x + SH_DECO_MARGIN)
        *x = geometry.x + SH_DECO_MARGIN;
    *y = geometry.y + SH_DECO_MARGIN;
}

/* The controls sit over the window's content, so they hide until the pointer nears its corner. */
bool in_deco_corner(struct sh_toplevel *toplevel, double x, double y) {
    int dx, dy;
    deco_position(toplevel, &dx, &dy);
    double left = toplevel->scene_tree->node.x + dx - SH_DECO_MARGIN;
    double top = toplevel->scene_tree->node.y + dy - SH_DECO_MARGIN;
    return x >= left && y >= top && x < left + 2 * SH_DECO_MARGIN + SH_DECO_WIDTH &&
           y < top + 2 * SH_DECO_MARGIN + SH_DECO_HEIGHT;
}

/* Keeps the controls and the border above the window's surfaces. A new node starts on top and
 * the surfaces sit in one subtree, so a frame node is out of place only when something else
 * was stacked over all of them; raising one that is already fine would still make the scene
 * recompute the window's whole tree on every commit. */
static void raise_frame_node(struct sh_toplevel *toplevel, struct wlr_scene_node *node) {
    struct wl_list *children = &toplevel->content->children;
    struct wlr_scene_node *top = wl_container_of(children->prev, top, link);
    if (top == node || (toplevel->deco && top == &toplevel->deco->node))
        return;
    for (int i = 0; i < 4; ++i)
        if (toplevel->border[i] && top == &toplevel->border[i]->node)
            return;
    wlr_scene_node_raise_to_top(node);
}

/* Adds, removes, or updates a window's controls to match what it asks for and its state. */
void refresh_decoration(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (!toplevel_mapped(toplevel) || !wants_decoration(toplevel)) {
        if (toplevel->deco)
            wlr_scene_node_destroy(&toplevel->deco->node);
        toplevel->deco = NULL;
        return;
    }
    enum sh_deco_part hovered =
        server->deco_hovered == toplevel ? server->deco_hovered_part : SH_DECO_NONE;
    struct wlr_buffer *buffer = deco_buffer(server, hovered);
    if (!buffer)
        return;
    if (!toplevel->deco) {
        toplevel->deco = wlr_scene_buffer_create(toplevel->content, buffer);
        if (!toplevel->deco)
            return;
        wlr_scene_buffer_set_dest_size(toplevel->deco, SH_DECO_WIDTH, SH_DECO_HEIGHT);
    } else if (toplevel->deco->buffer != buffer) {
        wlr_scene_buffer_set_buffer(toplevel->deco, buffer);
    }
    int x, y;
    deco_position(toplevel, &x, &y);
    wlr_scene_node_set_position(&toplevel->deco->node, x, y);
    raise_frame_node(toplevel, &toplevel->deco->node);
    wlr_scene_node_set_enabled(&toplevel->deco->node, server->deco_revealed == toplevel);
}

/* The tab strip of a group's shown window: one segment per member, the shown one lit. */
void refresh_tabs(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (!toplevel->group || toplevel->group_hidden || !toplevel_mapped(toplevel) ||
        toplevel->fullscreen || !groups_enabled(server)) {
        if (toplevel->tabs)
            wlr_scene_node_destroy(&toplevel->tabs->node);
        toplevel->tabs = NULL;
        return;
    }
    struct wlr_box g = toplevel_geometry(toplevel);
    int count = group_size(server, toplevel->group), active = group_index(toplevel);
    int hover = server->tabs_hovered == toplevel ? server->tabs_hovered_index : -1;
    float scale = 1;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->wlr_output->scale > scale)
            scale = output->wlr_output->scale;
    }
    int pixel_scale = (int)ceilf(scale);
    if (g.width < 1)
        return;
    if (!toplevel->tabs || toplevel->tabs_width != g.width || toplevel->tabs_count != count ||
        toplevel->tabs_active != active || toplevel->tabs_hover != hover ||
        toplevel->tabs_scale != pixel_scale) {
        size_t pixels = (size_t)g.width * pixel_scale * SH_TABS_HEIGHT * pixel_scale;
        uint32_t *data = calloc(pixels, sizeof(*data));
        if (!data)
            return;
        sh_tabs_paint(data, g.width, pixel_scale, count, active, hover);
        struct wlr_buffer *buffer =
            sh_pixel_buffer(data, g.width * pixel_scale, SH_TABS_HEIGHT * pixel_scale);
        if (!buffer)
            return; // it freed the pixels
        if (!toplevel->tabs)
            toplevel->tabs = wlr_scene_buffer_create(toplevel->content, buffer);
        else
            wlr_scene_buffer_set_buffer(toplevel->tabs, buffer);
        wlr_buffer_drop(buffer); // the scene holds it now
        if (!toplevel->tabs)
            return;
        toplevel->tabs_width = g.width;
        toplevel->tabs_count = count;
        toplevel->tabs_active = active;
        toplevel->tabs_hover = hover;
        toplevel->tabs_scale = pixel_scale;
        wlr_scene_buffer_set_dest_size(toplevel->tabs, g.width, SH_TABS_HEIGHT);
    }
    wlr_scene_node_set_position(&toplevel->tabs->node, g.x, g.y);
    wlr_scene_node_raise_to_top(&toplevel->tabs->node);
}

static void set_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct sh_toplevel *toplevel = data;
    if (buffer != toplevel->deco && buffer != toplevel->dim && buffer != toplevel->tabs)
        wlr_scene_buffer_set_opacity(buffer, toplevel->opacity);
}

static float rule_opacity(struct sh_toplevel *toplevel, const char *app_id, const char *title,
                          bool active) {
    struct sh_server *server = toplevel->server;
    app_id = app_id ? app_id : "";
    title = title ? title : "";
    struct sh_opacity_rule *cache = &toplevel->opacity_rule;
    if (cache->generation == server->config_generation && cache->active == active &&
        cache->app_id && cache->title && !strcmp(cache->app_id, app_id) &&
        !strcmp(cache->title, title))
        return cache->value;
    char *app_id_copy = strdup(app_id), *title_copy = strdup(title);
    ++server->stats.opacity_rules;
    cache->value = server->callbacks->opacity(server->callbacks->userdata, app_id, title, active);
    if (!app_id_copy || !title_copy) { // out of memory: leave the cache unusable
        free(app_id_copy);
        free(title_copy);
        app_id_copy = title_copy = NULL;
    }
    free(cache->app_id);
    free(cache->title);
    cache->app_id = app_id_copy;
    cache->title = title_copy;
    cache->active = active;
    cache->generation = server->config_generation;
    return cache->value;
}

/* The border around the window's geometry and its opacity, both following focus. Called on
 * every commit, since the geometry and the set of surfaces can change with any of them. */
void refresh_frame(struct sh_toplevel *toplevel) {
    if (!toplevel->scene_tree)
        return;
    overview_touch(toplevel->server, false); // a thumbnail of it may need copying again
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return;
#endif
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    bool mapped = toplevel_mapped(toplevel);
    bool active = server->focused_toplevel == toplevel;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    float opacity =
        toplevel->fullscreen || !mapped ? 1 : rule_opacity(toplevel, app_id, title, active);
    // Opacity and border color fade when focus changes; a window that is not yet shown, or
    // was hidden, takes them at once.
    int b = settings->border_width;
    // An urgent window is framed in its color even without a border: inside its edges, so the
    // layout does not move.
    bool urgent = toplevel->urgent && !active;
    bool inset = urgent && b == 0;
    bool shown = (b > 0 || inset) && mapped && !frameless(toplevel, toplevel_output(toplevel));
    const float *color = urgent ? settings->urgent_color
                                : active ? settings->border_active : settings->border_inactive;
    float target[SH_TWEEN_VALUES] = {opacity}, fading[SH_TWEEN_VALUES];
    if (shown)
        memcpy(target + 1, color, 4 * sizeof(float));
    sh_tween_track(server->animator, &toplevel->fade, SH_ANIM_FOCUS,
                   mapped && toplevel->shown && toplevel_visible(toplevel), target, fading,
                   fade_update, toplevel);
    // Peeking scales the opacity, fullscreen windows included.
    opacity = fading[0] * (float)(1 - sh_fade_value(&server->peek_fade, now_ms()) *
                                          (1 - settings->effects.peek_opacity));
    color = fading + 1;
    float pulsing[4];
    if (urgent) {
        // The border pulses over its fade to the urgent color; premultiplied, scaling all four
        // channels dims it.
        float pulse = urgent_pulse(toplevel, now_ms());
        for (int i = 0; i < 4; ++i)
            pulsing[i] = color[i] * pulse;
        color = pulsing;
    }
    // New subsurfaces start opaque, so a translucent window is revisited on every commit.
    if (opacity != toplevel->opacity || opacity < 1) {
        toplevel->opacity = opacity;
        wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node, set_buffer_opacity, toplevel);
    }
    update_dim(toplevel);
    refresh_decoration(toplevel); // the controls follow the window's width
    refresh_tabs(toplevel);

    // Windows on an output that tiles get rounded corners, floating ones too, clipping
    // everything drawn for them but the border, which rounds itself to match.
    struct wlr_box g = toplevel_geometry(toplevel);
    struct wlr_output *output = toplevel_output(toplevel);
    int radius = mapped && tiles_for(toplevel, output) && !frameless(toplevel, output)
                     ? settings->corner_radius
                     : 0;
#ifdef SHAODESK_ROUNDED_CORNERS
    wlr_scene_tree_set_rounded_clip(
        toplevel->content, radius > 0 ? &(struct wlr_box){0, 0, g.width, g.height} : NULL, radius);
#else
    radius = 0;
#endif

    // xdg-shell windows keep their scene tree while unmapped; the border must not.
    for (int i = 0; i < 4; ++i) {
        if (!shown) {
            if (toplevel->border[i])
                wlr_scene_node_destroy(&toplevel->border[i]->node);
            toplevel->border[i] = NULL;
            toplevel->frame_hole = 0;
            continue;
        }
        if (!toplevel->border[i])
            toplevel->border[i] = wlr_scene_rect_create(toplevel->content, 1, 1, color);
        if (!toplevel->border[i])
            return;
        wlr_scene_rect_set_color(toplevel->border[i], color);
    }
    if (!shown)
        return;
    // The scene tree's origin is the top-left corner of the window geometry.
    if (inset)
        b = g.width < 8 || g.height < 8 ? 1 : 2;
    const struct wlr_box outside[4] = {{-b, -b, g.width + 2 * b, b},
                                       {-b, g.height, g.width + 2 * b, b},
                                       {-b, 0, b, g.height},
                                       {g.width, 0, b, g.height}};
    const struct wlr_box inside[4] = {{0, 0, g.width, b},
                                      {0, g.height - b, g.width, b},
                                      {0, b, b, g.height - 2 * b},
                                      {g.width - b, b, b, g.height - 2 * b}};
    const struct wlr_box *sides = inset ? inside : outside;
    // Around rounded corners the first side is the whole frame, a hollow rounded rect whose
    // inner edge follows the window's corners; the other three are not needed.
    const struct wlr_box frame[4] = {
        inset ? (struct wlr_box){0, 0, g.width, g.height}
              : (struct wlr_box){-b, -b, g.width + 2 * b, g.height + 2 * b}};
    if (radius > 0)
        sides = frame;
    toplevel->frame_hole = radius > 0 ? b : 0;
    for (int i = 0; i < 4; ++i) {
        wlr_scene_node_set_position(&toplevel->border[i]->node, sides[i].x, sides[i].y);
        wlr_scene_rect_set_size(toplevel->border[i], sides[i].width, sides[i].height);
#ifdef SHAODESK_ROUNDED_CORNERS
        wlr_scene_rect_set_rounding(toplevel->border[i],
                                    i == 0 && radius > 0 ? radius + (inset ? 0 : b) : 0,
                                    i == 0 && radius > 0 ? b : 0);
#endif
        raise_frame_node(toplevel, &toplevel->border[i]->node);
    }
}

void forget_decoration(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->deco_hovered == toplevel)
        server->deco_hovered = NULL;
    if (server->deco_revealed == toplevel)
        server->deco_revealed = NULL;
    if (server->deco_pressed == toplevel)
        server->deco_pressed = NULL;
    if (server->tabs_hovered == toplevel)
        server->tabs_hovered = NULL;
    if (toplevel->xdg_toplevel && toplevel->tabs)
        wlr_scene_node_destroy(&toplevel->tabs->node);
    toplevel->tabs = NULL;
    // X11 windows lose it with their scene tree; xdg-shell ones keep theirs while unmapped.
    if (toplevel->xdg_toplevel && toplevel->deco)
        wlr_scene_node_destroy(&toplevel->deco->node);
    toplevel->deco = NULL;
}

static void fade_update(void *data) {
    refresh_frame(data);
}

/* The mode is sent with the first configure, or right away once the window has had one. */
static void decoration_set_mode(struct sh_toplevel *toplevel) {
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                                                decoration_mode(toplevel->decoration));
}

static void decoration_request_mode(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_mode);
    decoration_set_mode(toplevel);
}

static void decoration_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_destroy);
    wl_list_remove(&toplevel->decoration_mode.link);
    wl_list_remove(&toplevel->decoration_destroy.link);
    toplevel->decoration = NULL;
    refresh_decoration(toplevel);
}

void server_new_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    struct wlr_scene_tree *tree = decoration->toplevel->base->data;
    struct sh_node *node = tree ? tree->node.data : NULL;
    if (!node || node->kind != SH_NODE_TOPLEVEL)
        return;
    struct sh_toplevel *toplevel = node->owner;
    toplevel->decoration = decoration;
    add_listener(&decoration->events.request_mode, &toplevel->decoration_mode,
                 decoration_request_mode);
    add_listener(&decoration->events.destroy, &toplevel->decoration_destroy, decoration_destroy);
    decoration_set_mode(toplevel);
}
