/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
#include "server.h"

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

/* A tiled window moves into the tiling of the same output on its new workspace, or floats when
 * that workspace does not tile; a window that floated only because its workspace did not tile
 * joins the tiling of one that does. */
void set_toplevel_workspace(struct sh_toplevel *toplevel, int workspace) {
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
void move_toplevel_to_workspace(struct sh_server *server, struct sh_toplevel *toplevel,
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

void move_to_workspace(struct sh_server *server, int workspace) {
    move_toplevel_to_workspace(server, current_toplevel(server), workspace);
}
