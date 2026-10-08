/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Snapping a window dragged to an edge of its output (windows.snap), as Windows' Aero Snap and
 * KWin's quick tiling: the zone the pointer is in, and the drop that puts the window there. */
#include "server.h"

const char *snap_zone_name(enum sh_action zone) {
    switch (zone) {
    case SH_SNAP_LEFT:
        return "left";
    case SH_SNAP_RIGHT:
        return "right";
    case SH_MAXIMIZE:
        return "maximize";
    case SH_SNAP_TOP_LEFT:
        return "top_left";
    case SH_SNAP_TOP_RIGHT:
        return "top_right";
    case SH_SNAP_BOTTOM_LEFT:
        return "bottom_left";
    case SH_SNAP_BOTTOM_RIGHT:
        return "bottom_right";
    default:
        return "none";
    }
}

/* The edges of `output` that lead on to another output where the pointer is along them. */
static uint32_t shared_edges(struct sh_server *server, struct wlr_output *output, double x,
                             double y) {
    struct wlr_output_layout *layout = server->output_layout;
    struct wlr_box box;
    wlr_output_layout_get_box(layout, output, &box);
    uint32_t shared = 0;
    if (wlr_output_layout_output_at(layout, box.x - 1, y))
        shared |= SH_EDGE_LEFT;
    if (wlr_output_layout_output_at(layout, box.x + box.width, y))
        shared |= SH_EDGE_RIGHT;
    if (wlr_output_layout_output_at(layout, x, box.y - 1))
        shared |= SH_EDGE_TOP;
    if (wlr_output_layout_output_at(layout, x, box.y + box.height))
        shared |= SH_EDGE_BOTTOM;
    return shared;
}

/* Where dropping the grabbed window now would snap it, and on which output; SH_NONE where it
 * would not. The pointer decides, not the window: clients such as Firefox draw their tab strip
 * above their reported geometry. A window going into the tiling where it is dropped splits the
 * tile there instead, unless it is dropped at the top. */
static enum sh_action snap_zone(struct sh_server *server, struct wlr_output **where) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    const struct sh_settings *settings = server_settings(server);
    *where = NULL;
    if (!toplevel || server->cursor_mode != SH_CURSOR_MOVE || server->grab_fullscreen ||
        !settings->snap || server->locked)
        return SH_NONE;
    double x = server->cursor->x, y = server->cursor->y;
    struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, x, y);
    if (!output)
        return SH_NONE;
    enum sh_action zone =
        sh_snap_zone(usable_area(server, output), x, y, settings->snap_distance,
                     settings->snap_corners, shared_edges(server, output, x, y));
    if (zone != SH_NONE && zone != SH_MAXIMIZE && drop_tiles(server, output))
        zone = SH_NONE;
    if (zone != SH_NONE)
        *where = output;
    return zone;
}

/* Follows the pointer while a window is moved: the zone it is in, and the slot it stands for. */
void snap_follow(struct sh_server *server) {
    struct wlr_output *output;
    enum sh_action zone = snap_zone(server, &output);
    struct sh_rect slot = {0};
    if (zone != SH_NONE && !placed_slot(server, zone, output, &slot))
        zone = SH_NONE;
    server->snap.zone = zone;
    server->snap.slot = slot;
}

void snap_preview_hide(struct sh_server *server) {
    server->snap.zone = SH_NONE;
    server->snap.slot = (struct sh_rect){0};
}

/* Snaps the grabbed window as it is dropped, when the pointer is in a zone. A window that
 * floated when the drag began goes back there when it is restored; a tile lifted out goes
 * where it was dropped, at its floating size. Returns whether it snapped. */
bool snap_drop(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    struct wlr_output *output;
    enum sh_action zone = snap_zone(server, &output);
    snap_preview_hide(server);
    if (zone == SH_NONE)
        return false;
    bool lifted = server->grab_retile;
    server->grab_retile = false;
    struct wlr_box before = server->snap.start;
    place_by_hand_on(toplevel, zone, output);
    if (!lifted && toplevel->arranged && before.width > 0 && before.height > 0)
        toplevel->restore_box = before;
    wlr_log(WLR_DEBUG, "Snapped a dropped window: %s on %s", snap_zone_name(zone), output->name);
    return true;
}
