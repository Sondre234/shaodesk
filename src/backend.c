/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Derived from wlroots TinyWL 0.20.2; see vendor/tinywl/LICENSE. */
#define _GNU_SOURCE // accept4
#include "shaodesk/backend.h"
#include "shaodesk/effects.h"
#include "shaodesk/effects_scene.h"
#include "shaodesk/animation.h"
#include "shaodesk/curve.h"
#include "shaodesk/decoration.h"
#include "shaodesk/session.h"
#include "shaodesk/overview.h"
#include "shaodesk/overview_scene.h"
#include "shaodesk/tabs.h"
#include "shaodesk/sleep.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <dirent.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/config.h>
#if WLR_HAS_LIBINPUT_BACKEND
#include <libinput.h>
#include <wlr/backend/libinput.h>
#endif
#if WLR_HAS_SESSION
#include <wlr/backend/session.h>
#endif
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/xcursor.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#if SHAODESK_XWM_WAKER
#include <xcb/xfixes.h>
#endif
#endif
#include <xkbcommon/xkbcommon.h>

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

enum sh_cursor_mode {
    SH_CURSOR_PASSTHROUGH,
    SH_CURSOR_MOVE,
    SH_CURSOR_RESIZE,
};

enum sh_node_kind { SH_NODE_TOPLEVEL, SH_NODE_LAYER };
struct sh_node {
    enum sh_node_kind kind;
    void *owner;
};

#define SH_FRAME_RING 4096

/* Counters `get stats` reports, for the benchmark in tools/bench; cheap enough to keep on. */
struct sh_stats {
    uint64_t frames, frame_ns, frame_max_ns; /* output_frame calls and time spent in them */
    uint64_t commits, commit_ns;             /* window commits handled (refresh_frame) */
    uint64_t configures;                     /* windows placed by layouts (toplevel_configure) */
    uint64_t opacity_rules;                  /* times the window rules were matched for opacity */
    uint64_t motions, motion_ns;             /* pointer motion events handled and their time */
    uint64_t reflows, reflow_ns;             /* reflow_output runs that placed windows, and their time */
    /* The last SH_FRAME_RING frames: time spent in output_frame and time since the previous
     * frame, in microseconds, for `get frame_times` (percentiles in the benchmark). */
    uint32_t frame_us[SH_FRAME_RING], interval_us[SH_FRAME_RING];
    uint64_t last_frame_ns;
};

static uint64_t now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

enum sh_night_mode { SH_NIGHT_AUTO, SH_NIGHT_OFF, SH_NIGHT_ON };
#define NIGHT_LIGHT_TICK_MS 10000
#define NIGHT_LIGHT_LUT 1024
/* The overview (Expose). Thumbnails are scaled copies of the windows' scene nodes, kept live by
 * copying again when a window's fingerprint changes; see overview_scene.h. */
enum {
    OVERVIEW_MAX = 96,
    OVERVIEW_MINI_MAX = 128,
    OVERVIEW_WORKSPACES = 10,
    OVERVIEW_PAD = 6, /* the card around a thumbnail */
    OVERVIEW_TOP = 48 /* room above the strip for the shell's search box */
};
struct sh_toplevel;
struct sh_thumb {
    struct sh_toplevel *toplevel;
    struct wlr_scene_tree *tree;
    uint64_t fingerprint;
    double scale; /* the copy's scale; 0 before the first copy */
};
struct sh_overview {
    bool open;    /* taking input */
    bool visible; /* drawn: open, or gliding closed */
    bool closing;
    bool dirty; /* windows or workspaces changed: lay out again on the next step */
    bool armed; /* a step is scheduled */
    char output[64];
    struct wlr_box screen; /* the output, in layout coordinates */
    struct sh_rect area;   /* what the thumbnails use: the output less panels */
    struct wlr_scene_tree *tree, *strip, *cards_tree, *grid, *frames;
    struct wlr_scene_rect *backdrop, *frame[4];
    struct wl_event_source *timer;
    int viewed, current; /* the workspace the grid shows, and the one the output shows */
    int workspaces;
    char filter[128]; /* typed text; while set the grid lists matching windows everywhere */
    int count, selected;
    struct sh_toplevel *windows[OVERVIEW_MAX];
    struct sh_rect sizes[OVERVIEW_MAX];   /* the windows' sizes */
    struct sh_rect cells[OVERVIEW_MAX];   /* where the thumbnails rest */
    struct sh_rect origins[OVERVIEW_MAX]; /* where the windows are, or the cell without one */
    bool placed[OVERVIEW_MAX];            /* origins is a real place */
    struct wlr_scene_rect *cards[OVERVIEW_MAX];
    struct sh_thumb thumbs[OVERVIEW_MAX];
    int strip_count;
    struct sh_rect strip_cells[OVERVIEW_WORKSPACES];
    struct wlr_scene_rect *strip_back[OVERVIEW_WORKSPACES], *strip_mark[OVERVIEW_WORKSPACES][4];
    struct sh_thumb minis[OVERVIEW_MINI_MAX];
    int mini_count;
    int64_t started; /* milliseconds, CLOCK_MONOTONIC */
    int span;        /* milliseconds the current glide takes */
    double from, to, progress; /* 0: windows where they are, 1: thumbnails in the grid */
    int press;                 /* thumbnail under a pressed left button, else -1 */
    double press_x, press_y;
    bool dragging;
    struct sh_toplevel *dragged;
    int drop;         /* workspace cell under a dragged thumbnail, else -1 */
    uint32_t pressed; /* buttons the overview took; their releases are its too */
    double scroll;    /* wheel motion not yet enough to change the workspace */
    bool in_corner;   /* the pointer is in the hot corner: it opens the overview on entering */
};

struct sh_server {
    const struct sh_callbacks *callbacks;
    bool running;
    uint32_t grab_button;
    uint32_t bound_buttons; /* bit (code - BTN_MOUSE): pressed buttons a binding consumed */
    struct wlr_scene_tree *backgrounds;
    struct wlr_scene_tree *windows;
    struct wlr_scene_tree *fullscreen;
    struct wlr_scene_tree *fullscreen_cover; // above the panels
    struct wlr_scene_tree *unmanaged;
    struct sh_animator *animator;
    struct sh_stats stats;
    unsigned config_generation; /* counts configurations loaded, starting at 1 */
    struct {                    /* one hit test shared by the steps of a motion event */
        bool caching, valid;
        double x, y, sx, sy;
        struct wlr_scene_node *node;
    } hit;
    int reflow_held;            /* while positive, reflow_output waits for the holder's own call */
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready, new_xwayland_surface;
#if SHAODESK_XWM_WAKER
    xcb_connection_t *xwm_waker;
    xcb_atom_t waker_atom;
    xcb_window_t xwm_window;
    struct wl_event_source *waker_timer, *waker_input;
#endif
#endif
    /* Each output shows one of its own workspaces (from 0) and tiles automatically or not.
     * Kept by output name, so an output that goes away (all of them do on a VT switch) comes
     * back on the same workspace, tiling as it was. */
    struct {
        char name[64];
        int current;
        int previous;    // the workspace shown before `current`, or -1 for none yet
        int tiling;      // -1 until output_tiles decides it from the config
        bool configured; // what the config said then, so a reload only applies changes
    } output_workspaces[16];
    char active_output[64];           // of the last focused window, switched workspace, or click
    char placed_primary[32];          // the primary output the pointer was last put on, if any
    struct wlr_output *target_output; // set while a control request names an output
    struct sh_tiling *tiling;
    bool grab_retile;     // the grabbed window left the tiling to be moved; retile it on drop
    bool grab_fullscreen; // the grabbed window is fullscreen until it is dragged far enough
    unsigned scratchpad_serial;  // counts windows put into the scratchpad, to order them
    bool scratchpad_off_logged;  // said once that features.scratchpad is off
    struct wlr_output *grab_output; // where the grabbed window was when the grab began
    /* The window switcher: a list of every window taken as it opens, most recently focused
     * first. It stays open while `modifiers` (the opening binding's, less Shift) are all held,
     * or with none until confirmed or cancelled; the shell draws it on `output`. */
    struct {
        bool open;
        uint32_t modifiers;
        xkb_keysym_t key; // the opening binding's key, which selects the next window
        struct sh_toplevel *windows[128];
        int count, selected;
        char output[64];
    } switcher;
    struct sh_overview overview;
    struct wlr_scene_tree *overview_layer;
    /* Magnetic edges: guide lines over the windows while a dragged one is held by an edge. */
    struct wlr_scene_tree *guide_layer;
    struct wlr_scene_rect *guides[2]; /* a vertical and a horizontal line */

    /* Windows a session restore launched and has yet to place: the first new window with the
     * app ID takes the saved place, workspace and state, until the deadline (milliseconds on
     * the monotonic clock). */
    struct {
        struct sh_session_window window;
        int64_t deadline;
        bool used;
    } session_pending[32];

    /* Urgent windows (see windows.activation): the last order number given, and the timer that
     * redraws their pulsing borders while they pulse. */
    unsigned urgent_serial;
    struct wl_event_source *urgent_timer;
    /* Configuration files changing: the watch, and a short wait so that a burst of writes
     * (an editor saving through a temporary file) reloads once. */
    struct wl_event_source *config_watch;
    struct wl_event_source *config_timer;

    int control_fd;
    struct wl_list subscribers; // control clients receiving state changes
    char sent_state[8192];      // the state they last received
    char control_path[108];
    struct wl_event_source *control_source;
    struct wlr_scene_tree *layer_trees[4];
    struct wl_list layers;
    struct wl_listener new_layer_surface;
    struct sh_layer *focused_layer;
    struct sh_toplevel *focused_toplevel;
    struct wlr_foreign_toplevel_manager_v1 *foreign_manager;
    struct wlr_ext_foreign_toplevel_list_v1 *toplevel_list; // windows offered for screen sharing
    struct wl_listener new_capture_request;

    /* Session lock: `locked` outlives a crashed locker so the screen stays covered. */
    bool locked;
    struct sh_lock *lock;
    struct wlr_scene_tree *lock_tree, *lock_blanks;
    struct wl_listener new_lock;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_apply, output_test;
    struct wl_listener new_inhibitor;
    int inhibitors;
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    char pending_output_name[64]; // the name the next headless output takes, for tests
#if WLR_HAS_SESSION
    struct wlr_session *session;
    struct wl_listener session_active;
    int sleep_inhibitor; // logind inhibitor fd while this VT is in front, else -1
#endif
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_decoration;
    struct wl_list toplevels;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;
    struct wl_listener request_cursor;
    struct wl_listener request_set_shape;
    uint32_t shape_edges; // edges of the client's single-edge resize shape, else 0
    uint32_t shown_edges; // edges of the resize cursor currently shown for it
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection, request_set_primary_selection;
    struct wl_listener request_start_drag, start_drag;
    struct wlr_scene_tree *drag_icons; // follows the cursor during drag-and-drop
    struct wl_listener request_activate;
    struct wlr_relative_pointer_manager_v1 *relative_pointer;
    struct wlr_pointer_constraints_v1 *constraints;
    struct wlr_pointer_constraint_v1 *active_constraint; // on the keyboard-focused surface
    struct wl_listener new_constraint, keyboard_focus_change;
    struct wl_list keyboards;
    struct wl_list pointers; /* struct sh_pointer */
    enum sh_cursor_mode cursor_mode;
    struct sh_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;
    /* Window controls: shared buffers, the window whose controls are hovered (and which
     * button) or revealed, and a button pressed but not yet released. */
    struct wlr_buffer *deco_buffers[SH_DECO_FULLSCREEN + 1]; // by hovered part
    /* Peek: 0 to 1, how far windows have faded toward the desktop. It is held by the key with
     * evdev code `peek_keycode` on `peek_keyboard`, or toggled without one. */
    struct sh_fade peek_fade;
    bool peeking;
    double peek_applied; // what the windows were last given
    uint32_t peek_keycode;
    struct sh_keyboard *peek_keyboard;
    /* Night light: the schedule or an override picks a temperature; `night_transform` is the
     * matrix for it (NULL at neutral), handed to every output commit. */
    struct wl_event_source *night_timer;
    struct sh_fade zoom_fade; // the magnification, 1 for none
    double zoom_target, zoom_scroll;
    struct sh_corner_dwell corner_dwell; // hot corners
    struct wl_event_source *corner_timer;
    int night_mode; // enum sh_night_mode
    int night_kelvin;
    double night_clock; // minutes after midnight when fixed for testing, else -1
    struct wlr_color_transform *night_transform;
    struct wlr_buffer *black;   // stretched over windows to dim them; made on first use
    struct sh_toplevel *deco_hovered, *deco_revealed, *deco_pressed;
    enum sh_deco_part deco_hovered_part;
    enum sh_deco_part deco_pressed_part;
    unsigned group_serial;              // counts group members and groups, to name and order them
    struct sh_toplevel *tabs_hovered;   // the window whose tab strip the pointer is on
    int tabs_hovered_index;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;          /* enabled, in the layout */
    struct wl_list disabled_outputs; /* turned off by outputs.monitors */
    struct wl_listener new_output;
};

struct sh_output {
    struct wlr_box usable;
    int x, y;                /* arrangement before the shift to the layout origin */
    struct wlr_box previous; /* where arrange_outputs found it; empty when newly added */
    bool disabled;           /* listed in disabled_outputs */
    /* Settings a wlr-output-management client (wlr-randr, kanshi) applied at runtime. They
     * replace the configured monitor until the configuration is reloaded. */
    bool has_override;
    struct sh_monitor override;
    struct wlr_scene_rect *background, *lock_blank;
    bool lock_presented;
    /* Magnifier: the scene is drawn into `zoom_swapchain` and the output shows a part of the
     * newest of those buffers (`zoom_source`, locked) enlarged. */
    struct wlr_swapchain *zoom_swapchain;
    struct wlr_buffer *zoom_source;
    bool zoomed; // the last frame was magnified
    bool zoom_failed; // it could not be magnified this time; it shows 1x until the zoom is reset
    struct wl_list link;
    struct sh_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct sh_opacity_rule {
    unsigned generation; // server->config_generation it was computed under; 0 for never
    bool active;
    char *app_id, *title;
    float value;
};

struct sh_toplevel {
    struct sh_node node;
    enum sh_action arrangement;
    bool minimized;
    int workspace;   // one of the workspaces of `output`
    char output[64]; // the output the window was placed on, by name; empty before that
    /* Set while the window is away from an output that was unplugged: the output and workspace
     * it came from, and whether it was tiled there. It goes back when that output returns. */
    char home_output[64];
    int home_workspace;
    bool home_tiled;
    struct wlr_foreign_toplevel_handle_v1 *foreign;
    /* Window capture: a private scene holding only this window's surfaces, so sharing one
     * window never shows what overlaps it. */
    struct wlr_ext_foreign_toplevel_handle_v1 *listed;
    struct wlr_scene *capture_scene;
    struct wlr_ext_image_capture_source_v1 *capture_source;
    struct wl_listener title_changed, app_id_changed;
    struct wl_listener foreign_activate, foreign_close, foreign_maximize, foreign_minimize;
    struct wl_listener foreign_fullscreen;
    bool fullscreen;
    bool fullscreen_cover; // the client asked for fullscreen itself, so it covers the panels too
    struct wlr_box fullscreen_restore;
    struct wl_listener request_minimize;
    /* xdg-decoration: the window leaves its title bar to us and gets the window controls instead. */
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener decoration_mode, decoration_destroy;
    struct wlr_box restore_box;
    bool arranged;
    bool tiled;      // in the tiling tree; restore_box keeps its floating geometry
    bool floating;   // kept out of the tiling (dialogs, or toggled by the user)
    bool placed;     // floating only because it was snapped or maximized by hand
    bool tile_sized; // first configured at its predicted tile, so it has no floating size yet
    /* In the scratchpad: floating, and hidden (minimized) until scratchpad_show brings it to
     * the focused output. Hidden ones show in the order they were put there. */
    bool scratchpad;
    unsigned scratchpad_order;
    /* Shown on every workspace of its output, always floating; its workspace follows the
     * output's current one. `sticky_floating` is what `floating` was before, for unsticking. */
    bool sticky, sticky_floating;
    /* Asked for attention while it had no focus; cleared when it is focused or unmapped.
     * urgent_order says which asked first, urgent_since (milliseconds) when it began pulsing. */
    bool urgent;
    unsigned urgent_order;
    int64_t urgent_since;
    /* A window group: members share one slot and show one at a time (the others are
     * group_hidden, out of the tiling and off the screen). 0: not in a group. group_order
     * is the tab order. */
    unsigned group, group_order;
    bool group_hidden;
    /* Window swallowing: a terminal that lets a window it started take its place is
     * `swallowed` (hidden, and off the taskbar) and its `swallow_peer` is that window, whose
     * own peer is the terminal. */
    bool swallowed;
    struct sh_toplevel *swallow_peer;
    struct wlr_scene_buffer *tabs; // the strip of tabs over the shown member's top edge
    int tabs_width, tabs_count, tabs_active, tabs_hover, tabs_scale; // what `tabs` shows
    struct wl_list link;
    struct sh_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel; // NULL for X11 windows
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland_surface *xsurface; // NULL for xdg-shell windows
    bool unmanaged, associated;            // unmanaged: override-redirect menus and tooltips
    struct wl_listener x_associate, x_dissociate, x_configure, x_activate, x_geometry;
    struct wl_listener x_decorations, x_attention, x_hints;
    bool x_hint_urgent; // the client's WM_HINTS ask for attention
#endif
    /* scene_tree sits at the window's place; content holds everything drawn for it, so an
     * animation can move, scale, and fade it without changing where the window is. */
    struct wlr_scene_tree *scene_tree, *content;
    struct sh_anim anim;
    struct sh_tween fade; // opacity and border color following focus
    bool shown; // has opened (and started its opening animation) since it last mapped
    struct wlr_scene_buffer *deco;    // window controls; NULL when the client decorates itself
    struct wlr_scene_rect *border[4]; // top, bottom, left, right; NULL without a border
    int frame_hole; // with rounded corners, the width of the frame border[0] draws; else 0
    float opacity;                    // last applied to the window's buffers
    struct wlr_scene_buffer *dim;     // black over the window while it is dimmed, else NULL
    struct sh_fade dim_fade;          // how opaque that black is, and where it is heading
    /* The opacity the window rules gave for these inputs: matching regexes on every commit
     * would cost more than the commit, so it is redone only when one of them changes. */
    struct sh_opacity_rule opacity_rule;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct sh_layer {
    struct sh_node node;
    struct sh_server *server;
    struct wlr_layer_surface_v1 *surface;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wl_list link;
    struct wl_listener commit, map, unmap, destroy, new_popup;
};

struct sh_lock {
    struct sh_server *server;
    struct wlr_session_lock_v1 *lock;
    bool locked_sent;
    struct wl_listener new_surface, unlock, destroy;
};

struct sh_lock_surface {
    struct sh_server *server;
    struct wlr_session_lock_surface_v1 *surface;
    struct wlr_scene_tree *tree;
    struct wl_listener map, destroy;
};

struct sh_inhibitor {
    struct sh_server *server;
    struct wl_listener destroy;
};

struct sh_popup {
    struct sh_server *server;
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct sh_pointer {
    struct wl_list link;
    struct sh_server *server;
    struct wlr_input_device *device;
    struct wl_listener destroy;
};

struct sh_keyboard {
    bool consumed[KEY_MAX + 1];
    struct wl_list link;
    struct sh_server *server;
    struct wlr_keyboard *wlr_keyboard;
    /* A held key bound to a repeating action (keyboard resizing) runs it again at the
     * keyboard's repeat rate, as clients repeat keys themselves. */
    struct wl_event_source *repeat_timer;
    uint32_t repeat_keycode;
    enum sh_action repeat_action;
    int repeat_argument;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

static void arrange_layers(struct sh_server *server);
static void arrange_windows(struct sh_server *server, enum sh_action action);
static void place_by_hand(struct sh_toplevel *toplevel, enum sh_action action);
static void move_window(struct sh_server *server, enum sh_action action);
static void resize_window(struct sh_server *server, enum sh_action action, int amount);
static void begin_interactive(struct sh_toplevel *toplevel, enum sh_cursor_mode mode,
                              uint32_t edges);
static void create_popup(struct sh_server *server, struct wlr_xdg_popup *popup,
                         struct wlr_scene_tree *parent);
static struct wlr_output *find_output(struct sh_server *server, const char *name);
static void follow_output(struct sh_toplevel *toplevel);
static void lock_output_presented(struct sh_output *output);
static void notify_subscribers(struct sh_server *server);
static void request_launcher(struct sh_server *server);
static void request_palette(struct sh_server *server);
static void request_shell(struct sh_server *server, const char *what);
static void send_shell_line(struct sh_server *server, const char *line);
static void send_event(struct sh_server *server, const char *text, size_t length);
static void switcher_close(struct sh_server *server, int index);
static void set_default_cursor(struct sh_server *server);
static int64_t now_ms(void);
static void run_action(struct sh_server *server, enum sh_action action, int argument);
static void move_workspace_to_output(struct sh_server *server, const char *target);
static void swap_output_workspaces(struct sh_server *server, const char *target);
static void overview_touch(struct sh_server *server, bool relayout);
static void overview_forget(struct sh_toplevel *toplevel);
static void overview_dismiss(struct sh_server *server);
static void process_cursor_motion(struct sh_server *server, uint32_t time);
static void process_pointer_target(struct sh_server *server, uint32_t time);
static void refit_fullscreen(struct sh_server *server);
static void fit_fullscreen(struct sh_toplevel *toplevel);
static void rehome_tiles(struct sh_server *server);
static void reconfigure_tiling(struct sh_server *server);
static void configure_layouts(struct sh_server *server);
static void apply_output_layout(struct sh_server *server, const struct wlr_output *output);
static void schedule_evacuation(struct sh_server *server, struct sh_output *output);
static void evacuate_output(struct sh_server *server, const char *name, struct wlr_box gone,
                            bool keep_workspaces);
static void return_home_windows(struct sh_server *server);
static void pointer_follow(struct sh_toplevel *toplevel);
static enum sh_tile_layout toplevel_layout(struct sh_toplevel *toplevel);
static void reflow_output(struct sh_server *server, struct wlr_output *output);
static void refresh_frame(struct sh_toplevel *toplevel);
static void set_peek(struct sh_server *server, bool on);
static void night_light_update(struct sh_server *server);
static int night_light_tick(void *data);
static void hot_corner_check(struct sh_server *server);
static void zoom_by(struct sh_server *server, int steps);
static void zoom_moved(struct sh_server *server);
static struct sh_rect floating_area(struct sh_server *server, struct wlr_output *output);
static void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output);
static void restore_toplevel(struct sh_toplevel *toplevel);
static void reload_config(struct sh_server *server);
static void reset_cursor_mode(struct sh_server *server);
static void set_fullscreen(struct sh_toplevel *toplevel, bool fullscreen);
static void set_client_fullscreen(struct sh_toplevel *toplevel, bool fullscreen);
static struct wlr_scene_tree *fullscreen_tree(struct sh_toplevel *toplevel);
static void set_fullscreen_focus(struct sh_toplevel *toplevel, bool fullscreen, bool focus);
static bool output_tiles(struct sh_server *server, struct wlr_output *output);
static bool frameless(struct sh_toplevel *toplevel, struct wlr_output *output);
static struct wlr_output *box_output(struct sh_server *server, struct wlr_box box);
static void set_tiling(struct sh_server *server, struct wlr_output *output, bool enabled);
static void tile_toplevel(struct sh_toplevel *toplevel, struct wlr_output *output,
                          struct sh_toplevel *target, bool at_cursor);
static struct wlr_output *tiled_output(struct sh_toplevel *toplevel);
static struct wlr_output *toplevel_output(struct sh_toplevel *toplevel);
static void untile_toplevel(struct sh_toplevel *toplevel, bool restore);
static bool wants_tiling(struct sh_toplevel *toplevel, struct wlr_output *output);
static void tile_toplevel_at(struct sh_toplevel *toplevel, struct wlr_output *output,
                             struct sh_toplevel *target, bool has_point, double x, double y);
static bool toplevel_is_dialog(struct sh_toplevel *toplevel);
static pid_t toplevel_pid(struct sh_toplevel *toplevel);
static void switcher_forget(struct sh_toplevel *toplevel);
static void publish_toplevel(struct sh_toplevel *toplevel);
static void unpublish_toplevel(struct sh_toplevel *toplevel);
static void swallow_release(struct sh_toplevel *toplevel);
static void group_follow(struct sh_toplevel *toplevel);
static void group_show(struct sh_toplevel *toplevel);
static void group_detach(struct sh_toplevel *toplevel);
static void refresh_tabs(struct sh_toplevel *toplevel);

static const uint32_t ALL_EDGES = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;

static void add_listener(struct wl_signal *signal, struct wl_listener *listener,
                         wl_notify_func_t notify) {
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

static const struct sh_settings *server_settings(struct sh_server *server) {
    return server->callbacks->settings(server->callbacks->userdata);
}

static struct wlr_output *first_output(struct sh_server *server) {
    if (wl_list_empty(&server->outputs))
        return NULL;
    struct sh_output *first = wl_container_of(server->outputs.next, first, link);
    return first->wlr_output;
}

/* Window operations shared by xdg-shell and XWayland toplevels. */
static struct wlr_surface *toplevel_surface(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->surface;
#endif
    return toplevel->xdg_toplevel->base->surface;
}
static bool toplevel_mapped(struct sh_toplevel *toplevel) {
    struct wlr_surface *surface = toplevel_surface(toplevel);
    return toplevel->scene_tree && surface && surface->mapped;
}
static struct wlr_box toplevel_geometry(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return (struct wlr_box){0, 0, toplevel->xsurface->width, toplevel->xsurface->height};
#endif
    return toplevel->xdg_toplevel->base->geometry;
}
static void toplevel_set_activated(struct sh_toplevel *toplevel, bool activated) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_activate(toplevel->xsurface, activated);
        if (activated)
            wlr_xwayland_surface_restack(toplevel->xsurface, NULL, XCB_STACK_MODE_ABOVE);
        return;
    }
#endif
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, activated);
}
/* Positions the window in layout coordinates; X11 clients also learn the position. */
static void toplevel_configure(struct sh_toplevel *toplevel, int x, int y, int width, int height) {
    ++toplevel->server->stats.configures;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        if (width > 0 && height > 0)
            wlr_xwayland_surface_configure(toplevel->xsurface, x, y, width, height);
        follow_output(toplevel);
        return;
    }
#endif
    // Every configure makes the client draw again, so an unchanged size is not sent again.
    const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
    if (scheduled->width != width || scheduled->height != height)
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
    follow_output(toplevel);
}
static void toplevel_configure_box(struct sh_toplevel *toplevel, struct wlr_box box) {
    toplevel_configure(toplevel, box.x, box.y, box.width, box.height);
}
/* The window's position and size, as saved to restore it later. A size the client has not
 * committed yet counts, so placing a window again right after restoring it keeps the restore
 * size rather than the old one (X11 windows take their new size at once). */
static struct wlr_box toplevel_box(struct sh_toplevel *toplevel) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    if (toplevel->xdg_toplevel) {
        struct wlr_xdg_surface *base = toplevel->xdg_toplevel->base;
        const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
        bool pending = base->configure_idle || !wl_list_empty(&base->configure_list);
        if (pending && scheduled->width > 0 && scheduled->height > 0) {
            geometry.width = scheduled->width;
            geometry.height = scheduled->height;
        }
    }
    return (struct wlr_box){toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
                            geometry.width, geometry.height};
}
static void toplevel_set_position(struct sh_toplevel *toplevel, int x, int y) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface && toplevel->xsurface->width > 0 && toplevel->xsurface->height > 0)
        wlr_xwayland_surface_configure(toplevel->xsurface, x, y, toplevel->xsurface->width,
                                       toplevel->xsurface->height);
#endif
    follow_output(toplevel);
}
/* Tells the client and the taskbar whether the window is maximized, and which edges touch
 * a neighbour or the screen edge. */
static void toplevel_set_states(struct sh_toplevel *toplevel, bool maximized, uint32_t tiled) {
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign, maximized);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_set_maximized(toplevel->xsurface, maximized, maximized);
        return;
    }
#endif
    const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
    if (scheduled->maximized != maximized)
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, maximized);
    if (scheduled->tiled != tiled)
        wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, tiled);
}
static void toplevel_set_fullscreen_state(struct sh_toplevel *toplevel, bool fullscreen) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_set_fullscreen(toplevel->xsurface, fullscreen);
        return;
    }
#endif
    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);
}
/* Answers a denied client request by repeating the current state. */
static void toplevel_refresh(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        if (toplevel_mapped(toplevel))
            toplevel_set_position(toplevel, toplevel->scene_tree->node.x,
                                  toplevel->scene_tree->node.y);
        return;
    }
#endif
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}
static void toplevel_close(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_close(toplevel->xsurface);
        return;
    }
#endif
    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}
static const char *toplevel_title(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->title;
#endif
    return toplevel->xdg_toplevel->title;
}
static const char *toplevel_app_id(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->class;
#endif
    return toplevel->xdg_toplevel->app_id;
}
static bool toplevel_accepts_keyboard(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return wlr_xwayland_surface_icccm_input_model(toplevel->xsurface) !=
               WLR_ICCCM_INPUT_MODEL_NONE;
#endif
    return true;
}

/* The current workspace of the output named `name`, which need not be connected. */
static int output_slot(struct sh_server *server, const char *name) {
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
    return slot;
}

static int *output_workspace(struct sh_server *server, const char *name) {
    return &server->output_workspaces[output_slot(server, name)].current;
}

/* A window shows when its output shows its workspace. The output may be gone: its windows
 * keep their state until they are placed on another output. */
static bool toplevel_visible(struct sh_toplevel *toplevel) {
    return !toplevel->minimized && !toplevel->group_hidden && !toplevel->swallowed &&
           (toplevel->sticky || !toplevel->output[0] ||
            toplevel->workspace == *output_workspace(toplevel->server, toplevel->output));
}

/* Shows each output's current workspace; focus is left to the caller. Sticky windows move
 * along to it. */
static void show_workspaces(struct sh_server *server) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->sticky && toplevel->output[0])
            toplevel->workspace = *output_workspace(server, toplevel->output);
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
    }
    notify_subscribers(server);
}

static void set_active_output(struct sh_server *server, const char *name) {
    if (!name[0] || !strcmp(server->active_output, name))
        return;
    snprintf(server->active_output, sizeof(server->active_output), "%s", name);
    notify_subscribers(server);
}

/* Every workspace switch comes through here (actions, the panel, taskbar activation), so each
 * output remembers the workspace it showed before for workspace_back. */
static void show_workspace(struct sh_server *server, const char *output, int workspace) {
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
static struct wlr_output *focused_output(struct sh_server *server) {
    if (server->target_output)
        return server->target_output;
    struct wlr_output *output = find_output(server, server->active_output);
    if (!output)
        output = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
                                             server->cursor->y);
    return output ? output : first_output(server);
}

/* A window placed on another output joins that output's current workspace. */
static void set_toplevel_output(struct sh_toplevel *toplevel, struct wlr_output *output) {
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
static void follow_output(struct sh_toplevel *toplevel) {
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

static void deactivate_toplevel(struct sh_server *server) {
    if (!server->focused_toplevel)
        return;
    struct sh_toplevel *old = server->focused_toplevel;
    if (old->fullscreen)
        wlr_scene_node_reparent(&old->scene_tree->node, server->windows);
    toplevel_set_activated(old, false);
    if (old->foreign)
        wlr_foreign_toplevel_handle_v1_set_activated(old->foreign, false);
    server->focused_toplevel = NULL;
    refresh_frame(old);
}

/* Gives the window keyboard focus; `raise` also brings it to the front. */
/* Gives the surface keyboard focus even while the seat has no keyboard (headless, or before a
 * virtual keyboard connects), so the first keyboard to appear types into it. */
static void keyboard_enter(struct wlr_seat *seat, struct wlr_surface *surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard)
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
}

static void focus_toplevel_raise(struct sh_toplevel *toplevel, bool raise) {
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

static void focus_toplevel(struct sh_toplevel *toplevel) { focus_toplevel_raise(toplevel, true); }

/* Urgent windows. An unfocused window that asks for attention (xdg-activation, an X11 urgency
 * hint) is marked urgent under windows.activation = "urgent": its border pulses for a few
 * seconds and stays in the urgent color, the taskbar and workspace indicator show it, and
 * focus_urgent goes to the one that asked first. Focusing it, or unmapping it, ends it. */
enum { URGENT_PULSE_MS = 4000, URGENT_PULSE_PERIOD_MS = 1200, URGENT_TICK_MS = 40 };

/* How bright an urgent window's border is now: it starts at full strength and pulses down and
 * up for URGENT_PULSE_MS, then holds. Without animations it holds at once. */
static float urgent_pulse(struct sh_toplevel *toplevel, int64_t now) {
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
static void set_urgent(struct sh_toplevel *toplevel, bool urgent) {
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
static void activation_requested(struct sh_toplevel *toplevel) {
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
static bool hover_focuses(struct sh_server *server, struct sh_toplevel *toplevel) {
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

static void focus_previous(struct sh_server *server) {
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
static void focus_desktop(struct sh_server *server, struct wlr_output *output) {
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

static void focus_layer(struct sh_layer *layer) {
    if (layer->server->locked || layer->surface->current.keyboard_interactive ==
                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        return;
    struct sh_server *server = layer->server;
    deactivate_toplevel(server);
    server->focused_layer = layer;
    keyboard_enter(server->seat, layer->surface->surface);
}

static void minimize_toplevel(struct sh_toplevel *toplevel) {
    group_detach(toplevel); // a minimized window keeps no slot to share
    toplevel->minimized = true;
    untile_toplevel(toplevel, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, true);
    if (toplevel->server->focused_toplevel == toplevel)
        focus_previous(toplevel->server);
}

static struct sh_rect usable_area(struct sh_server *server, struct wlr_output *output) {
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
static struct wlr_box fullscreen_box(struct sh_toplevel *toplevel, struct wlr_output *output) {
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
static struct sh_toplevel *current_toplevel(struct sh_server *server) {
    if (server->focused_toplevel)
        return server->focused_toplevel;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel))
            return toplevel;
    }
    return NULL;
}

/* A tiled window moves into the tiling of the same output on its new workspace. */
static void set_toplevel_workspace(struct sh_toplevel *toplevel, int workspace) {
    bool retile = toplevel->tiled;
    struct wlr_output *output = tiled_output(toplevel);
    untile_toplevel(toplevel, false);
    toplevel->workspace = workspace;
    group_follow(toplevel);
    if (retile)
        tile_toplevel(toplevel, output, NULL, false);
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
static void switch_workspace(struct sh_server *server, struct wlr_output *output, int workspace) {
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
static void set_sticky(struct sh_toplevel *toplevel, bool sticky, bool retile) {
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
static struct sh_toplevel *toplevel_toward(struct sh_toplevel *from_toplevel, bool horizontal,
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
static void pointer_follow(struct sh_toplevel *toplevel) {
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
static bool groups_enabled(struct sh_server *server) {
    return server_settings(server)->groups;
}

static int group_size(struct sh_server *server, unsigned group) {
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
static int group_index(struct sh_toplevel *from) {
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
static void group_follow(struct sh_toplevel *toplevel) {
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
static void group_show(struct sh_toplevel *toplevel) {
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
static void group_detach(struct sh_toplevel *toplevel) {
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
static bool groupable(struct sh_toplevel *toplevel) {
    return toplevel && toplevel_mapped(toplevel) && !toplevel->sticky && !toplevel->scratchpad &&
           !toplevel->fullscreen && !toplevel->minimized && !toplevel->group_hidden
#if WLR_HAS_XWAYLAND
           && !toplevel->unmanaged
#endif
        ;
}

/* Makes `toplevel` a member of `group`, last in tab order. */
static void group_join(struct sh_toplevel *toplevel, unsigned group) {
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
static struct sh_toplevel *swallow_host(struct sh_toplevel *child, bool terminals_only) {
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
static bool swallow_wanted(struct sh_toplevel *child) {
    const struct sh_settings *settings = server_settings(child->server);
    return settings->swallow && !swallow_terminal(child) && !toplevel_is_dialog(child) &&
           !swallow_listed(settings->swallow_exceptions, settings->swallow_exception_count,
                           toplevel_app_id(child));
}

/* `child` takes the place of `host`, which hides and leaves the taskbar. */
static void swallow_attach(struct sh_toplevel *host, struct sh_toplevel *child) {
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
static void swallow_end(struct sh_toplevel *toplevel) {
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
static void switcher_close(struct sh_server *server, int index) {
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
static void switcher_forget(struct sh_toplevel *toplevel) {
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
static size_t overview_describe(struct sh_server *server, char *text, size_t size) {
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

static void overview_select(struct sh_server *server, int index) {
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
static void overview_touch(struct sh_server *server, bool relayout) {
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
static void overview_forget(struct sh_toplevel *toplevel) {
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
static void overview_dismiss(struct sh_server *server) {
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
static void overview_view(struct sh_server *server, int workspace) {
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

static void overview_set_filter(struct sh_server *server, const char *text) {
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

static bool overview_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
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

static bool overview_motion(struct sh_server *server) {
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
static void overview_hot_corner(struct sh_server *server) {
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
static bool overview_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event) {
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
static void hide_in_scratchpad(struct sh_toplevel *toplevel) {
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
static bool take_screenshot(struct sh_server *server, enum sh_screenshot_mode mode, char *error,
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
static void run_action(struct sh_server *server, enum sh_action action, int argument) {
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

static void set_default_cursor(struct sh_server *server) {
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

/* Whether `node` is a window's rounded frame and the layout position is inside its hole. */
static bool in_frame_hole(struct wlr_scene_node *node, double lx, double ly) {
    if (!node || node->type != WLR_SCENE_NODE_RECT)
        return false;
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data)
        tree = tree->node.parent;
    struct sh_node *owner = tree ? tree->node.data : NULL;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return false;
    struct sh_toplevel *toplevel = owner->owner;
    int hole = toplevel->frame_hole;
    if (hole <= 0 || !toplevel->border[0] || node != &toplevel->border[0]->node)
        return false;
    struct wlr_scene_rect *rect = toplevel->border[0];
    int x, y;
    wlr_scene_node_coords(node, &x, &y);
    return lx >= x + hole && lx < x + rect->width - hole && ly >= y + hole &&
           ly < y + rect->height - hole;
}

/* The topmost node at a layout position, where windows will be once their animations end:
 * input never waits for an animation, nor lands in the middle of one. */
static struct wlr_scene_node *scene_node_at(struct sh_server *server, double lx, double ly,
                                            double *sx, double *sy) {
    // A motion event asks the same question several times (window, controls, tabs, resize
    // band) with nothing moved in between; it answers once.
    if (server->hit.caching && server->hit.valid && server->hit.x == lx && server->hit.y == ly) {
        *sx = server->hit.sx;
        *sy = server->hit.sy;
        return server->hit.node;
    }
    bool animating = sh_animator_running(server->animator) > 0;
    if (animating)
        sh_animator_rest_at(server->animator, lx, ly);
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    // A rounded border is one hollow rect over the whole window, which hit testing takes as
    // solid: pointing through its hole reaches what is below, the window itself first.
    struct wlr_scene_node *hidden[8];
    int hidden_count = 0;
    while (hidden_count < 8 && in_frame_hole(node, lx, ly)) {
        node->enabled = false; // only for the next lookup: no damage, nothing redrawn
        hidden[hidden_count++] = node;
        node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    }
    while (hidden_count > 0)
        hidden[--hidden_count]->enabled = true;
    if (animating)
        sh_animator_resume(server->animator);
    if (server->hit.caching) {
        server->hit.valid = true;
        server->hit.x = lx;
        server->hit.y = ly;
        server->hit.sx = *sx;
        server->hit.sy = *sy;
        server->hit.node = node;
    }
    return node;
}

static struct sh_node *desktop_node_at(struct sh_server *server, double lx, double ly,
                                       struct wlr_surface **surface, double *sx, double *sy) {
    struct wlr_scene_node *node = scene_node_at(server, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree ? tree->node.data : NULL;
}

static struct sh_toplevel *desktop_toplevel_at(struct sh_server *server, double x, double y,
                                               struct wlr_surface **surface, double *sx,
                                               double *sy) {
    struct sh_node *node = desktop_node_at(server, x, y, surface, sx, sy);
    return node && node->kind == SH_NODE_TOPLEVEL ? node->owner : NULL;
}

/* Whether a panel (a layer surface above the windows) is under the point. It is no bare
 * desktop: pointing at it or clicking it leaves the focused window focused, so a taskbar on
 * another monitor still sees that window as the active one and a click on it minimizes it. */
static bool panel_at(struct sh_server *server, double x, double y) {
    struct wlr_surface *surface;
    double sx, sy;
    struct sh_node *node = desktop_node_at(server, x, y, &surface, &sx, &sy);
    if (!node || node->kind != SH_NODE_LAYER)
        return false;
    struct sh_layer *layer = node->owner;
    return layer->surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

/* Clients report exact corners only in a few pixels; a single-edge grab near the end of that
 * edge is almost always meant as a corner resize. */
static uint32_t corner_edges(struct sh_toplevel *toplevel, uint32_t edges) {
    struct wlr_cursor *cursor = toplevel->server->cursor;
    struct wlr_box geo_box = toplevel_geometry(toplevel);
    double left = toplevel->scene_tree->node.x + geo_box.x;
    double top = toplevel->scene_tree->node.y + geo_box.y;
    double margin_x = geo_box.width / 4.0 < 32 ? geo_box.width / 4.0 : 32;
    double margin_y = geo_box.height / 4.0 < 32 ? geo_box.height / 4.0 : 32;
    if ((edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) == 0) {
        if (cursor->x < left + margin_x)
            edges |= WLR_EDGE_LEFT;
        else if (cursor->x > left + geo_box.width - margin_x)
            edges |= WLR_EDGE_RIGHT;
    }
    if ((edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) == 0) {
        if (cursor->y < top + margin_y)
            edges |= WLR_EDGE_TOP;
        else if (cursor->y > top + geo_box.height - margin_y)
            edges |= WLR_EDGE_BOTTOM;
    }
    return edges;
}

/* Show a corner cursor wherever an edge grab would become a corner resize, so the pointer
 * matches what dragging will do. */
static void update_resize_cursor(struct sh_server *server, struct sh_toplevel *toplevel) {
    if (!server->shape_edges || !toplevel)
        return;
    uint32_t edges = corner_edges(toplevel, server->shape_edges);
    if (edges == server->shown_edges)
        return;
    server->shown_edges = edges;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wlr_xcursor_get_resize_name(edges));
}

static uint32_t shape_edges(enum wp_cursor_shape_device_v1_shape shape) {
    switch (shape) {
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
        return WLR_EDGE_TOP;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
        return WLR_EDGE_BOTTOM;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
        return WLR_EDGE_LEFT;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
        return WLR_EDGE_RIGHT;
    default:
        return 0;
    }
}

static void cursor_request_set_shape(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_shape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER ||
        server->seat->pointer_state.focused_client != event->seat_client)
        return;
    server->shape_edges = shape_edges(event->shape);
    server->shown_edges = 0;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                           wlr_cursor_shape_v1_name(event->shape));
    if (server->cursor_mode == SH_CURSOR_PASSTHROUGH) {
        struct wlr_surface *surface;
        double sx, sy;
        update_resize_cursor(server, desktop_toplevel_at(server, server->cursor->x,
                                                         server->cursor->y, &surface, &sx, &sy));
    }
}

static void magnet_hide_guides(struct sh_server *server) {
    for (int i = 0; i < 2; ++i) {
        if (server->guides[i])
            wlr_scene_node_set_enabled(&server->guides[i]->node, false);
    }
}

static void reset_cursor_mode(struct sh_server *server) {
    magnet_hide_guides(server);
    // A window dropped on another output takes up that output's corners.
    if (server->grabbed_toplevel)
        refresh_frame(server->grabbed_toplevel);
    server->cursor_mode = SH_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = NULL;
    server->grab_retile = false;
    server->grab_output = NULL;
    server->grab_fullscreen = false;
}

/* The pointer reached the top edge of its output, or a panel along it. The pointer decides, not
 * the window: clients such as Firefox draw their tab strip above their reported geometry. */
static bool dropped_at_top(struct sh_server *server) {
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    return output && server->cursor->y < usable_area(server, output).y + 1;
}

/* Dropping a window dragged out of the tiling splits the tile under the pointer; dropping one
 * at the top of the screen maximizes it instead, below the panels, like Super+Shift+Up. */
static void finish_grab(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    bool maximize = toplevel && server->cursor_mode == SH_CURSOR_MOVE &&
                    !server->grab_fullscreen && dropped_at_top(server);
    if (maximize) {
        server->grab_retile = false;
        place_by_hand(toplevel, SH_MAXIMIZE);
        return;
    }
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    // A window floating only because it was snapped or maximized, or because its output does
    // not tile, joins the tiling of another output it is dropped on; one floated on purpose
    // stays floating. A tile dropped on an output that does not tile floats there.
    if (toplevel && server->cursor_mode == SH_CURSOR_MOVE && !toplevel->tiled &&
        !toplevel->sticky && (!toplevel->floating || toplevel->placed) && output &&
        output != server->grab_output && output_tiles(server, output)) {
        toplevel->floating = toplevel->placed = false;
        server->grab_retile = true;
    }
    if (server->grab_retile && toplevel && wants_tiling(toplevel, output))
        tile_toplevel(toplevel, output, NULL, true);
    server->grab_retile = false;
}

/* Pointer travel that turns a press on a fullscreen window into a drag out of fullscreen. */
#define SH_DRAG_THRESHOLD 8

/* Magnetic edges (windows.magnet). While a floating window is dragged or resized, an edge of it
 * that comes within `distance` of an edge of the output, of the area panels leave free, or of
 * another window (one whose extent beside it overlaps the dragged window's) lands on it. The
 * position always follows from where the pointer is, not from where the window was, so the
 * window stays held until the pointer has moved `distance` away and then follows it again. */
#define MAGNET_LINES 80

/* An edge a window can stick to: a vertical line at x = `at` (or a horizontal one at y = `at`)
 * spanning [from, to] along the other axis, or, with `outer`, the whole of it. */
struct magnet_line {
    int at, from, to;
    bool outer;
};
struct magnet_lines {
    struct magnet_line line[MAGNET_LINES];
    int count;
};

/* The edge of the window that landed, and where. */
struct magnet_hit {
    bool found;
    int delta;
    struct magnet_line line;
};

static void magnet_add(struct magnet_lines *lines, int at, int from, int to, bool outer) {
    if (lines->count < MAGNET_LINES)
        lines->line[lines->count++] = (struct magnet_line){at, from, to, outer};
}

/* The lines to consider for a window occupying `frame` (the frame the eye sees, border
 * included): vertical ones in `x`, horizontal ones in `y`. */
static void magnet_collect(struct sh_server *server, struct sh_toplevel *toplevel,
                           struct wlr_output *output, struct wlr_box frame,
                           struct magnet_lines *x, struct magnet_lines *y) {
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box full;
    wlr_output_layout_get_box(server->output_layout, output, &full);
    struct sh_rect free_area = usable_area(server, output);
    int xs[] = {full.x, full.x + full.width, free_area.x, free_area.x + free_area.width};
    int ys[] = {full.y, full.y + full.height, free_area.y, free_area.y + free_area.height};
    for (size_t i = 0; i < sizeof(xs) / sizeof(*xs); ++i) {
        magnet_add(x, xs[i], 0, 0, true);
        magnet_add(y, ys[i], 0, 0, true);
    }
    int left = frame.x, right = frame.x + frame.width, top = frame.y,
        bottom = frame.y + frame.height;
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (other == toplevel || !toplevel_mapped(other) || !toplevel_visible(other) ||
            other->fullscreen || other->minimized || toplevel_output(other) != output)
            continue;
        struct wlr_box b = toplevel_box(other);
        int o_left = b.x - border, o_right = b.x + b.width + border, o_top = b.y - border,
            o_bottom = b.y + b.height + border;
        if (top - distance <= o_bottom && bottom + distance >= o_top) {
            magnet_add(x, o_left, o_top, o_bottom, false);
            magnet_add(x, o_right, o_top, o_bottom, false);
        }
        if (left - distance <= o_right && right + distance >= o_left) {
            magnet_add(y, o_top, o_left, o_right, false);
            magnet_add(y, o_bottom, o_left, o_right, false);
        }
    }
}

/* Moves the edges `low` and `high` (each when allowed) onto the nearest line within reach. */
static struct magnet_hit magnet_best(const struct magnet_lines *lines, int low, int high,
                                     bool use_low, bool use_high, int distance) {
    struct magnet_hit hit = {0};
    for (int i = 0; i < lines->count; ++i) {
        for (int side = 0; side < 2; ++side) {
            if (!(side ? use_high : use_low))
                continue;
            int delta = lines->line[i].at - (side ? high : low);
            if (abs(delta) > distance || (hit.found && abs(delta) >= abs(hit.delta)))
                continue;
            hit.found = true;
            hit.delta = delta;
            hit.line = lines->line[i];
        }
    }
    return hit;
}

static void magnet_draw(struct sh_server *server, int index, bool show, struct wlr_box box) {
    if (!show) {
        if (server->guides[index])
            wlr_scene_node_set_enabled(&server->guides[index]->node, false);
        return;
    }
    const struct sh_settings *settings = server_settings(server);
    if (!server->guides[index])
        server->guides[index] = wlr_scene_rect_create(server->guide_layer, box.width, box.height,
                                                      settings->magnet_guide_color);
    struct wlr_scene_rect *rect = server->guides[index];
    wlr_scene_rect_set_size(rect, box.width, box.height);
    wlr_scene_rect_set_color(rect, settings->magnet_guide_color);
    wlr_scene_node_set_position(&rect->node, box.x, box.y);
    wlr_scene_node_set_enabled(&rect->node, true);
}

/* Draws the guide lines for edges that landed: `frame` is the window's frame now. */
static void magnet_show(struct sh_server *server, struct wlr_box frame, struct magnet_hit x,
                        struct magnet_hit y) {
    const int thickness = 3; // an edge of the output still shows two of them
    bool guides = server_settings(server)->magnet_guides;
    int left = frame.x, right = frame.x + frame.width, top = frame.y,
        bottom = frame.y + frame.height;
    // A line covers the extent of the windows it joins.
    int from = x.found && !x.line.outer && x.line.from < top ? x.line.from : top;
    int to = x.found && !x.line.outer && x.line.to > bottom ? x.line.to : bottom;
    magnet_draw(server, 0, guides && x.found,
                (struct wlr_box){x.line.at - thickness / 2, from, thickness, to - from});
    from = y.found && !y.line.outer && y.line.from < left ? y.line.from : left;
    to = y.found && !y.line.outer && y.line.to > right ? y.line.to : right;
    magnet_draw(server, 1, guides && y.found,
                (struct wlr_box){from, y.line.at - thickness / 2, to - from, thickness});
}

/* The modifiers held on any keyboard, virtual ones (wtype, tests) included. */
static uint32_t held_modifiers(struct sh_server *server) {
    uint32_t mods = 0;
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        mods |= wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    }
    return mods;
}

/* The output the magnetism works on, or NULL when it is off for this drag. */
static struct wlr_output *magnet_output(struct sh_server *server, struct sh_toplevel *toplevel) {
    const struct sh_settings *settings = server_settings(server);
    uint32_t mods = held_modifiers(server);
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (!settings->magnet || settings->magnet_distance <= 0 || !output || toplevel->tiled ||
        server->grab_retile || server->grab_fullscreen ||
        (settings->magnet_bypass && (mods & settings->magnet_bypass) == settings->magnet_bypass)) {
        magnet_hide_guides(server);
        return NULL;
    }
    return output;
}

/* Adjusts the position (x, y) a drag asks for. */
static void magnet_snap(struct sh_server *server, struct sh_toplevel *toplevel, int *x, int *y) {
    struct wlr_output *output = magnet_output(server, toplevel);
    if (!output)
        return;
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box box = toplevel_box(toplevel);
    // The frame the eye sees: the border is drawn outside the window's geometry.
    struct wlr_box frame = {*x - border, *y - border, box.width + 2 * border,
                            box.height + 2 * border};
    struct magnet_lines vertical = {0}, horizontal = {0};
    magnet_collect(server, toplevel, output, frame, &vertical, &horizontal);
    struct magnet_hit hx = magnet_best(&vertical, frame.x, frame.x + frame.width, true, true,
                                       distance);
    struct magnet_hit hy = magnet_best(&horizontal, frame.y, frame.y + frame.height, true, true,
                                       distance);
    if (hx.found)
        *x += hx.delta, frame.x += hx.delta;
    if (hy.found)
        *y += hy.delta, frame.y += hy.delta;
    magnet_show(server, frame, hx, hy);
}

/* Adjusts the edges a resize asks for (the sides in `edges` are the ones being moved). */
static void magnet_snap_resize(struct sh_server *server, struct sh_toplevel *toplevel,
                               uint32_t edges, int *left, int *top, int *right, int *bottom) {
    struct wlr_output *output = magnet_output(server, toplevel);
    if (!output)
        return;
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box frame = {*left - border, *top - border, *right - *left + 2 * border,
                            *bottom - *top + 2 * border};
    struct magnet_lines vertical = {0}, horizontal = {0};
    magnet_collect(server, toplevel, output, frame, &vertical, &horizontal);
    struct magnet_hit hx = magnet_best(&vertical, frame.x, frame.x + frame.width,
                                       edges & WLR_EDGE_LEFT, edges & WLR_EDGE_RIGHT, distance);
    struct magnet_hit hy = magnet_best(&horizontal, frame.y, frame.y + frame.height,
                                       edges & WLR_EDGE_TOP, edges & WLR_EDGE_BOTTOM, distance);
    // A window keeps a pixel of width and height however the edge is pulled.
    if (hx.found) {
        if (edges & WLR_EDGE_LEFT && *left + hx.delta < *right)
            *left += hx.delta, frame.x += hx.delta, frame.width -= hx.delta;
        else if (edges & WLR_EDGE_RIGHT && *right + hx.delta > *left)
            *right += hx.delta, frame.width += hx.delta;
        else
            hx.found = false;
    }
    if (hy.found) {
        if (edges & WLR_EDGE_TOP && *top + hy.delta < *bottom)
            *top += hy.delta, frame.y += hy.delta, frame.height -= hy.delta;
        else if (edges & WLR_EDGE_BOTTOM && *bottom + hy.delta > *top)
            *bottom += hy.delta, frame.height += hy.delta;
        else
            hy.found = false;
    }
    magnet_show(server, frame, hx, hy);
}

static void process_cursor_move(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (server->grab_fullscreen) {
        if (hypot(server->cursor->x - server->grab_x, server->cursor->y - server->grab_y) <
            SH_DRAG_THRESHOLD)
            return;
        // Leave fullscreen, then drag the window at the size it had before, like a snapped one.
        reset_cursor_mode(server);
        set_fullscreen(toplevel, false);
        if (!toplevel->arranged && !toplevel->tiled) {
            toplevel->restore_box = toplevel_box(toplevel);
            toplevel->arranged = true;
        }
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
    }
    int x = (int)(server->cursor->x - server->grab_x), y = (int)(server->cursor->y - server->grab_y);
    magnet_snap(server, toplevel, &x, &y);
    toplevel_set_position(toplevel, x, y);
}

static void process_cursor_resize(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    if (!toplevel->tiled)
        magnet_snap_resize(server, toplevel, server->resize_edges, &new_left, &new_top,
                           &new_right, &new_bottom);
    if (toplevel->tiled) {
        // Resizing a tile moves the splits beside the dragged edges instead.
        struct sh_rect rect = {new_left, new_top, new_right - new_left, new_bottom - new_top};
        struct wlr_output *output = tiled_output(toplevel);
        if (sh_tiling_resize(server->tiling, toplevel, server->resize_edges, rect) && output)
            reflow_output(server, output);
        return;
    }
    struct wlr_box geo_box = toplevel_geometry(toplevel);
    toplevel_configure(toplevel, new_left - geo_box.x, new_top - geo_box.y, new_right - new_left,
                       new_bottom - new_top);
}

/* Windows are decorated by the server (no title bar, just the window controls) unless they ask to draw
 * their own frame, as frameless Electron apps like Discord do. */
static enum wlr_xdg_toplevel_decoration_v1_mode
decoration_mode(struct wlr_xdg_toplevel_decoration_v1 *decoration) {
    return decoration->requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

static bool wants_decoration(struct sh_toplevel *toplevel) {
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
static bool in_deco_corner(struct sh_toplevel *toplevel, double x, double y) {
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
static void refresh_decoration(struct sh_toplevel *toplevel) {
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
static void refresh_tabs(struct sh_toplevel *toplevel) {
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

static void fade_update(void *data) {
    refresh_frame(data);
}

static int64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Asks every output for a frame, so that fades keep advancing while nothing else changes. */
static void schedule_frames(struct sh_server *server) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) wlr_output_schedule_frame(output->wlr_output);
}

/* Puts the dimming black at its current opacity, or takes it away once it has faded out. */
static void apply_dim(struct sh_toplevel *toplevel, int64_t now) {
    double value = sh_fade_value(&toplevel->dim_fade, now);
    bool wanted = value > 0.001 || sh_fade_active(&toplevel->dim_fade, now) ||
                  toplevel->dim_fade.to > 0;
    if (!wanted || !toplevel->scene_tree) {
        if (toplevel->dim && toplevel->scene_tree)
            wlr_scene_node_destroy(&toplevel->dim->node);
        toplevel->dim = NULL;
        return;
    }
    struct sh_server *server = toplevel->server;
    if (!toplevel->dim) {
        if (!server->black)
            server->black = sh_black_buffer();
        if (!server->black)
            return;
        toplevel->dim = sh_dim_create(toplevel->content, server->black);
        if (!toplevel->dim)
            return;
    }
    // The scene tree's origin is the top-left corner of the window geometry.
    struct wlr_box g = toplevel_geometry(toplevel);
    wlr_scene_node_set_position(&toplevel->dim->node, 0, 0);
    wlr_scene_buffer_set_dest_size(toplevel->dim, g.width, g.height);
    // Peeking clears the dimming with everything else.
    value *= 1 - sh_fade_value(&server->peek_fade, now);
    wlr_scene_buffer_set_opacity(toplevel->dim, (float)value);
    wlr_scene_node_raise_to_top(&toplevel->dim->node);
}

/* windows.dim_inactive: a window without focus fades toward the configured darkness. */
static void update_dim(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    bool dimmed = settings->dim_inactive > 0 && toplevel_mapped(toplevel) &&
                  !toplevel->fullscreen && server->focused_toplevel != toplevel;
    double target = dimmed ? settings->dim_inactive : 0;
    int64_t now = now_ms();
    if (target != toplevel->dim_fade.to) {
        bool fades = settings->animations && toplevel_mapped(toplevel);
        // A window that gains focus just as it maps has barely started to dim: no fade back.
        if (target == 0 && sh_fade_value(&toplevel->dim_fade, now) < 0.01)
            fades = false;
        sh_fade_to(&toplevel->dim_fade, target, now, fades ? settings->dim_duration : 0);
        schedule_frames(server);
    }
    apply_dim(toplevel, now);
}

/* Advances the fades; true while one is still running. */
static bool tick_effects(struct sh_server *server) {
    int64_t now = now_ms();
    bool running = sh_fade_active(&server->peek_fade, now) || sh_fade_active(&server->zoom_fade, now);
    struct sh_toplevel *toplevel;
    double peek = sh_fade_value(&server->peek_fade, now);
    if (peek != server->peek_applied) {
        server->peek_applied = peek;
        wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    }
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->dim)
            continue;
        running = running || sh_fade_active(&toplevel->dim_fade, now);
        apply_dim(toplevel, now);
    }
    return running;
}

/* The temperature the schedule or an override asks for right now. */
static int night_light_target(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    if (server->night_mode == SH_NIGHT_OFF || (server->night_mode == SH_NIGHT_AUTO && !fx->night_light))
        return SH_KELVIN_NEUTRAL;
    if (server->night_mode == SH_NIGHT_ON)
        return fx->night_kelvin;
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    double minute = server->night_clock >= 0
                        ? server->night_clock
                        : local.tm_hour * 60 + local.tm_min + local.tm_sec / 60.0;
    struct sh_night_schedule schedule = {fx->day_kelvin, fx->night_kelvin, fx->sunrise, fx->sunset,
                                         fx->transition};
    if (fx->sunrise < 0) {
        bool polar_day = false;
        if (!sh_solar_times(fx->latitude, fx->longitude, local.tm_year + 1900, local.tm_mon + 1,
                            local.tm_mday, local.tm_gmtoff / 3600.0, &schedule.sunrise,
                            &schedule.sunset, &polar_day))
            return polar_day ? fx->day_kelvin : fx->night_kelvin;
    }
    return sh_night_kelvin(&schedule, minute);
}

/* Applies the temperature for now to every output, and keeps the clock ticking while the
 * schedule is in charge. */
static void night_light_update(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    int kelvin = night_light_target(server);
    if (kelvin != server->night_kelvin) {
        struct wlr_color_transform *transform = NULL;
        if (kelvin != SH_KELVIN_NEUTRAL) {
            // The same three-table form a wlr-gamma-control client would hand over, which
            // outputs turn into their hardware gamma tables.
            uint16_t ramp[3 * NIGHT_LIGHT_LUT];
            sh_gamma_ramp(sh_kelvin_to_rgb(kelvin), NIGHT_LIGHT_LUT, ramp);
            transform = wlr_color_transform_init_lut_3x1d(
                NIGHT_LIGHT_LUT, ramp, ramp + NIGHT_LIGHT_LUT, ramp + 2 * NIGHT_LIGHT_LUT);
        }
        if (transform || kelvin == SH_KELVIN_NEUTRAL) {
            if (server->night_transform)
                wlr_color_transform_unref(server->night_transform);
            server->night_transform = transform;
            server->night_kelvin = kelvin;
            struct sh_output *output;
            wl_list_for_each(output, &server->outputs, link) {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }
    if (server->night_timer)
        wl_event_source_timer_update(
            server->night_timer,
            server->night_mode == SH_NIGHT_AUTO && fx->night_light && server->night_clock < 0
                ? NIGHT_LIGHT_TICK_MS
                : 0);
}

static int night_light_tick(void *data) {
    night_light_update(data);
    return 0;
}

/* Hot corners: runs what a corner is bound to once the pointer has rested in it. */
static bool output_has_fullscreen(struct sh_server *server, struct wlr_output *output) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen && toplevel_mapped(toplevel) && toplevel_visible(toplevel) &&
            toplevel_output(toplevel) == output)
            return true;
    }
    return false;
}

static void hot_corner_check(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    int corner = -1;
    if (fx->corner_mask && !server->locked && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        !server->seat->drag) {
        struct wlr_output *output =
            wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
        struct wlr_box box;
        if (output && !output_has_fullscreen(server, output)) {
            wlr_output_layout_get_box(server->output_layout, output, &box);
            corner = sh_corner_at(server->cursor->x - box.x, server->cursor->y - box.y, box.width,
                                  box.height, fx->corner_size);
            if (corner >= 0 && !(fx->corner_mask & (1U << corner)))
                corner = -1;
        }
    }
    int64_t now = now_ms();
    int fired = sh_corner_dwell_update(&server->corner_dwell, corner, now, fx->corner_delay);
    if (fired >= 0) {
        int argument = 0;
        enum sh_action action = server->callbacks->hot_corner(server->callbacks->userdata, fired,
                                                              &argument);
        if (action != SH_NONE)
            run_action(server, action, argument);
    }
    if (server->corner_timer) {
        int wait = sh_corner_dwell_wait(&server->corner_dwell, now, fx->corner_delay);
        wl_event_source_timer_update(server->corner_timer, wait > 0 ? wait : 0);
    }
}

static int hot_corner_tick(void *data) {
    hot_corner_check(data);
    return 0;
}

/* Magnifier. `zoom_by` moves the target a step (or back to 1x for 0 steps) and the level
 * eases there; outputs draw themselves through `output_commit_zoomed` while it is above 1. */
static double zoom_level(struct sh_server *server, int64_t now) {
    return sh_fade_value(&server->zoom_fade, now);
}

static void zoom_by(struct sh_server *server, int steps) {
    const struct sh_settings *settings = server_settings(server);
    double target = steps == 0 ? 1
                               : sh_zoom_level(server->zoom_target, settings->effects.zoom_step,
                                               steps, settings->effects.zoom_max);
    if (target == server->zoom_target)
        return;
    server->zoom_target = target;
    int64_t now = now_ms();
    sh_fade_to(&server->zoom_fade, target, now,
               settings->animations ? settings->effects.zoom_duration : 0);
    tick_effects(server);
    schedule_frames(server);
}

/* The view follows the pointer, so a frame is due whenever it moves while magnified. */
static void zoom_moved(struct sh_server *server) {
    if (server->zoom_target > 1 || server->zoom_fade.to > 1)
        schedule_frames(server);
}

static void output_release_zoom(struct sh_output *output) {
    if (output->zoom_source)
        wlr_buffer_unlock(output->zoom_source);
    output->zoom_source = NULL;
    if (output->zoom_swapchain)
        wlr_swapchain_destroy(output->zoom_swapchain);
    output->zoom_swapchain = NULL;
}

/* Draws the scene into a private buffer and puts the part of it around the pointer on the
 * output, enlarged; false (nothing committed) when that cannot be done. */
static bool output_commit_zoomed(struct sh_output *output, struct wlr_scene_output *scene_output,
                                 const struct wlr_scene_output_state_options *options,
                                 double level) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    if (!wlr_output->enabled || wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL ||
        box.width <= 0 || box.height <= 0)
        return false;
    if (output->zoom_swapchain && (output->zoom_swapchain->width != wlr_output->width ||
                                   output->zoom_swapchain->height != wlr_output->height))
        output_release_zoom(output);
    if (!output->zoom_swapchain &&
        !wlr_output_configure_primary_swapchain(wlr_output, NULL, &output->zoom_swapchain))
        return false;
    if (!output->zoomed)
        wlr_damage_ring_add_whole(&scene_output->damage_ring);
    struct wlr_scene_output_state_options scene_options = *options;
    scene_options.swapchain = output->zoom_swapchain;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    bool done = false;
    struct wlr_texture *texture = NULL;
    if (!wlr_scene_output_build_state(scene_output, &state, &scene_options))
        goto out;
    if (state.committed & WLR_OUTPUT_STATE_BUFFER) {
        if (output->zoom_source)
            wlr_buffer_unlock(output->zoom_source);
        output->zoom_source = wlr_buffer_lock(state.buffer);
    }
    if (!output->zoom_source)
        goto out;
    texture = wlr_texture_from_buffer(server->renderer, output->zoom_source);
    if (!texture)
        goto out;
    struct sh_view view = sh_zoom_view(level, box.width, box.height, server->cursor->x - box.x,
                                       server->cursor->y - box.y);
    double sx = (double)output->zoom_source->width / box.width;
    double sy = (double)output->zoom_source->height / box.height;
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(wlr_output, &state, NULL);
    if (!pass)
        goto out;
    wlr_render_pass_add_texture(
        pass, &(struct wlr_render_texture_options){
                  .texture = texture,
                  .src_box = {view.x * sx, view.y * sy, view.width * sx, view.height * sy},
                  .dst_box = {0, 0, wlr_output->width, wlr_output->height},
                  .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                  .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
              });
    if (!wlr_render_pass_submit(pass))
        goto out;
    // Everything changed as far as the output is concerned, not just what the scene redrew.
    state.committed &= ~WLR_OUTPUT_STATE_DAMAGE;
    done = wlr_output_commit_state(wlr_output, &state);
out:
    if (texture)
        wlr_texture_destroy(texture);
    wlr_output_state_finish(&state);
    if (done)
        output->zoomed = true;
    return done;
}

/* Starts or ends peeking. Windows fade over peek.duration, or at once without animations. */
static void set_peek(struct sh_server *server, bool on) {
    if (server->peeking == on)
        return;
    const struct sh_settings *settings = server_settings(server);
    server->peeking = on;
    sh_fade_to(&server->peek_fade, on ? 1 : 0, now_ms(),
               settings->animations ? settings->effects.peek_duration : 0);
    if (!on) {
        server->peek_keycode = 0;
        server->peek_keyboard = NULL;
    }
    tick_effects(server);
    schedule_frames(server);
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
static void refresh_frame(struct sh_toplevel *toplevel) {
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
    int radius = mapped && output_tiles(server, output) && !frameless(toplevel, output)
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

static void forget_decoration(struct sh_toplevel *toplevel) {
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

/* The window whose controls are at (x, y), and which part of it. */
static struct sh_toplevel *deco_at(struct sh_server *server, double x, double y,
                                   enum sh_deco_part *part) {
    double sx, sy;
    struct wlr_scene_node *node = scene_node_at(server, x, y, &sx, &sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER || !node->parent)
        return NULL;
    struct sh_node *owner = node->parent->node.data;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return NULL;
    struct sh_toplevel *toplevel = owner->owner;
    if (!toplevel->deco || &toplevel->deco->node != node)
        return NULL;
    *part = sh_decoration_part_at(sx, sy);
    return *part == SH_DECO_NONE ? NULL : toplevel;
}

/* Windows without a title bar move by a press along their top edge, as if they had one. */
enum { SH_DRAG_STRIP = 6 };

/* `toplevel`, if the pointer is on its surface within the strip along its top edge. */
static struct sh_toplevel *drag_strip_at(struct sh_toplevel *toplevel, struct wlr_surface *surface,
                                         double y) {
    if (!toplevel || !toplevel->deco || toplevel->fullscreen || !surface ||
        wlr_surface_get_root_surface(surface) != toplevel_surface(toplevel))
        return NULL;
    double top = toplevel->scene_tree->node.y + toplevel_geometry(toplevel).y;
    return y >= top && y < top + SH_DRAG_STRIP ? toplevel : NULL;
}

/* Server-decorated windows draw no frame of their own to grab, so a band just outside their
 * edges (and border) resizes them, as a client's frame would. */
enum { SH_RESIZE_BAND = 8 };

/* The window whose resize band is at (x, y), and which of its edges that band is along. The
 * band is only reachable where nothing is drawn above the desktop: not over another window, nor
 * over a panel. */
static struct sh_toplevel *resize_band_at(struct sh_server *server, double x, double y,
                                          uint32_t *edges) {
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct sh_node *node = desktop_node_at(server, x, y, &surface, &sx, &sy);
    if (node && node->kind == SH_NODE_TOPLEVEL)
        return NULL;
    if (node && node->kind == SH_NODE_LAYER) {
        struct sh_layer *layer = node->owner;
        if (layer->surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP)
            return NULL;
    }
    int reach = server_settings(server)->border_width + SH_RESIZE_BAND;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel_mapped(toplevel) || !toplevel_visible(toplevel) || toplevel->fullscreen ||
            !wants_decoration(toplevel))
            continue;
        struct wlr_box g = toplevel_geometry(toplevel);
        double left = toplevel->scene_tree->node.x + g.x;
        double top = toplevel->scene_tree->node.y + g.y;
        double right = left + g.width, bottom = top + g.height;
        if (x < left - reach || x >= right + reach || y < top - reach || y >= bottom + reach)
            continue;
        uint32_t found = (x < left ? WLR_EDGE_LEFT : x >= right ? WLR_EDGE_RIGHT : 0) |
                         (y < top ? WLR_EDGE_TOP : y >= bottom ? WLR_EDGE_BOTTOM : 0);
        if (!found)
            continue;
        *edges = corner_edges(toplevel, found);
        return toplevel;
    }
    return NULL;
}

/* The window whose tab strip is at (x, y), and which tab, counting from 0. */
static struct sh_toplevel *tabs_at(struct sh_server *server, double x, double y, int *index) {
    double sx, sy;
    struct wlr_scene_node *node = scene_node_at(server, x, y, &sx, &sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER || !node->parent)
        return NULL;
    struct sh_node *owner = node->parent->node.data;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return NULL;
    struct sh_toplevel *toplevel = owner->owner;
    if (!toplevel->tabs || &toplevel->tabs->node != node)
        return NULL;
    *index = sh_tabs_index_at(toplevel->tabs_width, toplevel->tabs_count, sx);
    return *index < 0 ? NULL : toplevel;
}

static void set_tabs_hovered(struct sh_server *server, struct sh_toplevel *toplevel, int index) {
    struct sh_toplevel *old = server->tabs_hovered;
    if (old == toplevel && server->tabs_hovered_index == index)
        return;
    server->tabs_hovered = toplevel;
    server->tabs_hovered_index = index;
    if (old && old != toplevel)
        refresh_tabs(old);
    if (toplevel)
        refresh_tabs(toplevel);
}

/* The member of the group that is tab `index`. */
static struct sh_toplevel *group_tab(struct sh_toplevel *from, int index) {
    struct sh_toplevel *member;
    wl_list_for_each(member, &from->server->toplevels, link) {
        if (member->group == from->group && group_index(member) == index)
            return member;
    }
    return NULL;
}

static void set_deco_hovered(struct sh_server *server, struct sh_toplevel *toplevel,
                             enum sh_deco_part part) {
    struct sh_toplevel *old = server->deco_hovered;
    if (old == toplevel && server->deco_hovered_part == part)
        return;
    server->deco_hovered = toplevel;
    server->deco_hovered_part = part;
    if (old && old != toplevel)
        refresh_decoration(old);
    if (toplevel)
        refresh_decoration(toplevel);
}

static void deco_activate(struct sh_toplevel *toplevel, enum sh_deco_part part) {
    switch (part) {
    case SH_DECO_CLOSE:
        toplevel_close(toplevel);
        break;
    case SH_DECO_MINIMIZE:
        minimize_toplevel(toplevel);
        break;
    case SH_DECO_FULLSCREEN:
        set_fullscreen(toplevel, !toplevel->fullscreen);
        break;
    default:
        break;
    }
}

static void process_cursor_motion(struct sh_server *server, uint32_t time) {
    hot_corner_check(server);
    zoom_moved(server);
    if (server->seat->drag)
        wlr_scene_node_set_position(&server->drag_icons->node, server->cursor->x,
                                    server->cursor->y);
    overview_hot_corner(server);
    if (overview_motion(server))
        return;
    if (server->cursor_mode == SH_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    }
    if (server->cursor_mode == SH_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    server->hit.caching = true;
    server->hit.valid = false;
    process_pointer_target(server, time);
    server->hit.caching = false;
}

/* What the pointer is over after moving: window, controls, tabs, resize band, focus. */
static void process_pointer_target(struct sh_server *server, uint32_t time) {
    double sx, sy;
    struct wlr_seat *seat = server->seat;
    struct sh_toplevel *revealed = server->deco_revealed;
    if (revealed && !in_deco_corner(revealed, server->cursor->x, server->cursor->y)) {
        server->deco_revealed = NULL;
        refresh_decoration(revealed);
        server->hit.valid = false; // the controls came out of the scene
    }
    struct wlr_surface *surface = NULL;
    struct sh_toplevel *toplevel =
        desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (toplevel && toplevel->deco && !server->deco_revealed &&
        in_deco_corner(toplevel, server->cursor->x, server->cursor->y)) {
        server->deco_revealed = toplevel;
        refresh_decoration(toplevel);
        server->hit.valid = false; // the controls went into the scene
    }
    enum sh_deco_part part;
    struct sh_toplevel *decorated = deco_at(server, server->cursor->x, server->cursor->y, &part);
    set_deco_hovered(server, decorated, decorated ? part : SH_DECO_NONE);
    if (decorated) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    int tab;
    struct sh_toplevel *tabbed = tabs_at(server, server->cursor->x, server->cursor->y, &tab);
    set_tabs_hovered(server, tabbed, tabbed ? tab : -1);
    if (tabbed) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    uint32_t band_edges;
    if (!surface && resize_band_at(server, server->cursor->x, server->cursor->y, &band_edges)) {
        if (server->shape_edges || server->shown_edges != band_edges) {
            server->shape_edges = 0;
            server->shown_edges = band_edges;
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                                   wlr_xcursor_get_resize_name(band_edges));
        }
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    if (toplevel && hover_focuses(server, toplevel))
        focus_toplevel_raise(toplevel, false);
    else if (!toplevel && server_settings(server)->focus_follows_mouse &&
             seat->pointer_state.button_count == 0 && !wlr_seat_pointer_has_grab(seat) &&
             !wlr_seat_keyboard_has_grab(seat) &&
             !panel_at(server, server->cursor->x, server->cursor->y))
        focus_desktop(server, wlr_output_layout_output_at(server->output_layout,
                                                          server->cursor->x, server->cursor->y));
    if (drag_strip_at(toplevel, surface, server->cursor->y)) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
        update_resize_cursor(server, toplevel);
    } else {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server->relative_pointer, server->seat, (uint64_t)event->time_msec * 1000, event->delta_x,
        event->delta_y, event->unaccel_dx, event->unaccel_dy);
    double dx = event->delta_x, dy = event->delta_y;
    struct wlr_pointer_constraint_v1 *constraint = server->active_constraint;
    if (constraint && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        server->seat->pointer_state.focused_surface == constraint->surface) {
        if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
            return; // The client only wants the relative motion sent above.
        double sx = server->seat->pointer_state.sx, sy = server->seat->pointer_state.sy;
        double confined_x, confined_y;
        if (wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &confined_x,
                               &confined_y)) {
            dx = confined_x - sx;
            dy = confined_y - sy;
        }
    }
    uint64_t started = now_ns();
    wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
    process_cursor_motion(server, event->time_msec);
    ++server->stats.motions;
    server->stats.motion_ns += now_ns() - started;
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    uint64_t started = now_ns();
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
    ++server->stats.motions;
    server->stats.motion_ns += now_ns() - started;
}

/* Actions that work on the focused window; over the bare desktop, a button binding skips them
 * rather than act on a window the pointer is not on. */
static bool action_targets_window(enum sh_action action) {
    switch (action) {
    case SH_CLOSE:
    case SH_FULLSCREEN:
    case SH_TOGGLE_FLOATING:
    case SH_TOGGLE_STICKY:
    case SH_SWALLOW_TOGGLE:
    case SH_GROUP_TOGGLE:
    case SH_GROUP_NEXT:
    case SH_GROUP_PREV:
    case SH_UNGROUP:
    case SH_MOVE_TO_WORKSPACE:
    case SH_SNAP_LEFT:
    case SH_SNAP_RIGHT:
    case SH_MAXIMIZE:
    case SH_RESTORE:
    case SH_MOVE_LEFT:
    case SH_MOVE_RIGHT:
    case SH_MOVE_UP:
    case SH_MOVE_DOWN:
    case SH_MOVE_TO_SCRATCHPAD:
    case SH_PROMOTE:
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV:
    case SH_RESIZE_LEFT:
    case SH_RESIZE_RIGHT:
    case SH_RESIZE_UP:
    case SH_RESIZE_DOWN:
        return true;
    default:
        return false;
    }
}

/* Runs the binding for a pressed button, if one matches what lies under the pointer, and
 * swallows both the press and its release. Returns false when the click belongs to a client. */
static bool handle_button_binding(struct sh_server *server,
                                  const struct wlr_pointer_button_event *event) {
    if (event->button < BTN_MOUSE || event->button >= BTN_MOUSE + 32)
        return false;
    uint32_t bit = 1u << (event->button - BTN_MOUSE);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(server->bound_buttons & bit))
            return false;
        server->bound_buttons &= ~bit;
        return true;
    }
    if (server->locked || server->grab_button || server->deco_pressed)
        return false;
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct sh_node *node =
        desktop_node_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    struct sh_toplevel *toplevel = NULL;
    enum sh_pointer_target target = SH_POINTER_DESKTOP;
    if (node && node->kind == SH_NODE_TOPLEVEL) {
        toplevel = node->owner;
        target = SH_POINTER_WINDOW;
    } else if (node && node->kind == SH_NODE_LAYER) {
        struct sh_layer *layer = node->owner;
        if (layer->surface->current.layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND)
            target = SH_POINTER_OTHER;
    } else if (surface) {
        target = SH_POINTER_OTHER; // a popup, or an unmanaged X11 window
    }
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    int argument = 0;
    enum sh_action action = server->callbacks->button(
        server->callbacks->userdata, mods, event->button, target, app_id ? app_id : "", &argument);
    if (action == SH_NONE)
        return false;
    server->bound_buttons |= bit;
    if (toplevel)
        focus_toplevel(toplevel);
    if (toplevel || !action_targets_window(action))
        run_action(server, action, argument);
    return true;
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if ((server->overview.open || server->overview.pressed) && overview_button(server, event))
        return;
    if (server->deco_pressed && event->button == BTN_LEFT &&
        event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // A dot acts on release, and only if the pointer is still on it.
        struct sh_toplevel *pressed = server->deco_pressed;
        server->deco_pressed = NULL;
        enum sh_deco_part part;
        if (deco_at(server, server->cursor->x, server->cursor->y, &part) == pressed &&
            part == server->deco_pressed_part)
            deco_activate(pressed, part);
        process_cursor_motion(server, event->time_msec);
        return;
    }
    if (server->grab_button == event->button && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        server->grab_button = 0;
        finish_grab(server);
        reset_cursor_mode(server);
        process_cursor_motion(server, event->time_msec);
        return;
    }
    if (handle_button_binding(server, event))
        return;
    if (!server->locked && !server->deco_pressed) {
        int tab;
        struct sh_toplevel *tabbed = tabs_at(server, server->cursor->x, server->cursor->y, &tab);
        if (tabbed) {
            // A press on a tab brings that window forward; the release is ignored.
            if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && event->button == BTN_LEFT) {
                struct sh_toplevel *member = group_tab(tabbed, tab);
                focus_toplevel(member ? member : tabbed);
            }
            return;
        }
    }
    enum sh_deco_part part;
    struct sh_toplevel *decorated =
        event->state == WL_POINTER_BUTTON_STATE_PRESSED && !server->locked && !server->deco_pressed
            ? deco_at(server, server->cursor->x, server->cursor->y, &part)
            : NULL;
    if (!decorated && event->state == WL_POINTER_BUTTON_STATE_PRESSED && !server->locked &&
        !server->deco_pressed && event->button == BTN_LEFT) {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct sh_toplevel *toplevel =
            desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        toplevel = drag_strip_at(toplevel, surface, server->cursor->y);
        if (toplevel) {
            focus_toplevel(toplevel);
            server->grab_button = BTN_LEFT;
            begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
            return;
        }
        uint32_t edges;
        toplevel = surface ? NULL
                           : resize_band_at(server, server->cursor->x, server->cursor->y, &edges);
        if (toplevel) {
            focus_toplevel(toplevel);
            server->grab_button = BTN_LEFT;
            begin_interactive(toplevel, SH_CURSOR_RESIZE, edges);
            return;
        }
    }
    if (decorated) {
        focus_toplevel(decorated);
        if (event->button != BTN_LEFT)
            return;
        server->deco_pressed = decorated;
        server->deco_pressed_part = part;
        return;
    }
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        struct wlr_output *clicked = wlr_output_layout_output_at(
            server->output_layout, server->cursor->x, server->cursor->y);
        if (clicked)
            set_active_output(server, clicked->name);
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct sh_node *node =
            desktop_node_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        struct sh_toplevel *toplevel = node && node->kind == SH_NODE_TOPLEVEL ? node->owner : NULL;
        if (server->locked) {
            node = NULL; // Only lock surfaces, which have no desktop node, are reachable.
            toplevel = NULL;
        }
        if (!toplevel && !server->locked && !panel_at(server, server->cursor->x, server->cursor->y))
            focus_desktop(server, clicked);
        if (toplevel)
            focus_toplevel(toplevel);
        else if (node && node->kind == SH_NODE_LAYER)
            focus_layer(node->owner);
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
        uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
        const struct sh_settings *settings = server_settings(server);
        if (toplevel && (mods & settings->mouse_modifier) &&
            (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            server->grab_button = event->button;
            uint32_t edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
            if (toplevel->tiled) {
                // A tile's outer edges cannot move, so resize from the corner nearest the pointer.
                struct wlr_box geometry = toplevel_geometry(toplevel);
                double center_x = toplevel->scene_tree->node.x + geometry.x + geometry.width / 2.0;
                double center_y = toplevel->scene_tree->node.y + geometry.y + geometry.height / 2.0;
                edges = (server->cursor->x < center_x ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) |
                        (server->cursor->y < center_y ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
            }
            begin_interactive(toplevel,
                              event->button == BTN_LEFT ? SH_CURSOR_MOVE : SH_CURSOR_RESIZE, edges);
            return;
        }
    }
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        finish_grab(server);
        reset_cursor_mode(server);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (overview_axis(server, event))
        return;
    struct wlr_keyboard *held = wlr_seat_get_keyboard(server->seat);
    unsigned zoom_mask = server_settings(server)->effects.zoom_scroll_modifier;
    if (zoom_mask && held && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        (wlr_keyboard_get_modifiers(held) & zoom_mask) == zoom_mask) {
        // A wheel notch is about 15 units; touchpads send small amounts that add up.
        server->zoom_scroll += event->delta;
        while (server->zoom_scroll <= -10) {
            server->zoom_scroll += 10;
            zoom_by(server, 1);
        }
        while (server->zoom_scroll >= 10) {
            server->zoom_scroll -= 10;
            zoom_by(server, -1);
        }
        return;
    }
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_frame);

    wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;

    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);
    struct sh_stats *stats = &output->server->stats;
    uint64_t started = now_ns();

    sh_animator_tick(output->server->animator);
    overview_touch(output->server, false); // windows that move or fade move their thumbnails
    if (tick_effects(output->server))
        wlr_output_schedule_frame(output->wlr_output);
    struct wlr_scene_output_state_options night = {.color_transform = output->server->night_transform};
    double level = zoom_level(output->server, now_ms());
    bool zoomed = false;
    struct wlr_output *pointed = wlr_output_layout_output_at(
        output->server->output_layout, output->server->cursor->x, output->server->cursor->y);
    if (level > 1.0005 && pointed == output->wlr_output && !output->zoom_failed) {
        zoomed = output_commit_zoomed(output, scene_output, &night, level);
        if (!zoomed) {
            wlr_log(WLR_ERROR, "Cannot magnify %s; showing it at 1x", output->wlr_output->name);
            output->zoom_failed = true;
        }
    }
    if (!zoomed) {
        if (output->zoomed) {
            wlr_damage_ring_add_whole(&scene_output->damage_ring);
            output->zoomed = false;
        }
        if (!(level > 1.0005)) {
            output_release_zoom(output);
            output->zoom_failed = false; // the next zoom tries again
        }
        wlr_scene_output_commit(scene_output, &night);
    }
    lock_output_presented(output);
    uint64_t spent = now_ns() - started;
    ++stats->frames;
    stats->frame_ns += spent;
    if (spent > stats->frame_max_ns)
        stats->frame_max_ns = spent;
    size_t slot = (stats->frames - 1) % SH_FRAME_RING;
    stats->frame_us[slot] = (uint32_t)(spent / 1000);
    stats->interval_us[slot] = stats->last_frame_ns ? (uint32_t)((started - stats->last_frame_ns) / 1000) : 0;
    stats->last_frame_ns = started;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void update_backgrounds(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        wlr_scene_node_set_position(&output->background->node, box.x, box.y);
        wlr_scene_rect_set_size(output->background, box.width, box.height);
        wlr_scene_rect_set_color(output->background, settings->background);
        wlr_scene_node_set_position(&output->lock_blank->node, box.x, box.y);
        wlr_scene_rect_set_size(output->lock_blank, box.width, box.height);
    }
}

/* ext-session-lock-v1: an opaque cover hides the desktop from the moment a lock
 * starts; lock surfaces sit above it, and `locked` is sent once every output has
 * presented a covered frame. */
static void send_locked_if_presented(struct sh_server *server) {
    struct sh_lock *lock = server->lock;
    if (!lock || lock->locked_sent)
        return;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->lock_presented)
            return;
    }
    lock->locked_sent = true;
    wlr_session_lock_v1_send_locked(lock->lock);
}

static void lock_output_presented(struct sh_output *output) {
    if (!output->server->locked || output->lock_presented)
        return;
    output->lock_presented = true;
    send_locked_if_presented(output->server);
}

static void lock_surface_map(struct wl_listener *listener, void *data) {
    struct sh_lock_surface *lock_surface = wl_container_of(listener, lock_surface, map);
    struct sh_server *server = lock_surface->server;
    if (server->locked && !server->seat->keyboard_state.focused_surface)
        keyboard_enter(server->seat, lock_surface->surface->surface);
    process_cursor_motion(server, 0);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
    struct sh_lock_surface *lock_surface = wl_container_of(listener, lock_surface, destroy);
    struct sh_server *server = lock_surface->server;
    if (server->seat->keyboard_state.focused_surface == lock_surface->surface->surface) {
        wlr_seat_keyboard_clear_focus(server->seat);
        // Hand the keyboard to another lock surface, if one remains.
        struct wlr_session_lock_surface_v1 *other;
        if (server->lock) {
            wl_list_for_each(other, &server->lock->lock->surfaces, link) {
                if (other != lock_surface->surface && other->surface->mapped) {
                    keyboard_enter(server->seat, other->surface);
                    break;
                }
            }
        }
    }
    wl_list_remove(&lock_surface->map.link);
    wl_list_remove(&lock_surface->destroy.link);
    wlr_scene_node_destroy(&lock_surface->tree->node);
    free(lock_surface);
}

static void lock_new_surface(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, new_surface);
    struct sh_server *server = lock->server;
    struct wlr_session_lock_surface_v1 *surface = data;
    struct sh_lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
    if (!lock_surface)
        return;
    lock_surface->server = server;
    lock_surface->surface = surface;
    lock_surface->tree = wlr_scene_subsurface_tree_create(server->lock_tree, surface->surface);
    if (!lock_surface->tree) {
        free(lock_surface);
        return;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, surface->output, &box);
    wlr_scene_node_set_position(&lock_surface->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(surface, box.width, box.height);
    add_listener(&surface->surface->events.map, &lock_surface->map, lock_surface_map);
    add_listener(&surface->events.destroy, &lock_surface->destroy, lock_surface_destroy);
}

static void lock_unlock(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, unlock);
    struct sh_server *server = lock->server;
    server->locked = false;
    wlr_scene_node_set_enabled(&server->lock_tree->node, false);
    wlr_seat_keyboard_clear_focus(server->seat);
    if (server->focused_toplevel && !server->focused_toplevel->minimized)
        focus_toplevel(server->focused_toplevel);
    else
        focus_previous(server);
    process_cursor_motion(server, 0);
    wlr_log(WLR_INFO, "Session unlocked");
}

static void lock_destroy(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, destroy);
    struct sh_server *server = lock->server;
    if (server->locked)
        wlr_log(WLR_ERROR, "Lock client vanished; the session stays locked until a new lock");
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);
    server->lock = NULL;
    free(lock);
}

static void server_new_lock(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_lock);
    struct wlr_session_lock_v1 *wlr_lock = data;
    struct sh_lock *lock = server->lock ? NULL : calloc(1, sizeof(*lock));
    if (!lock) {
        wlr_session_lock_v1_destroy(wlr_lock); // Another locker is active: `finished`.
        return;
    }
    lock->server = server;
    lock->lock = wlr_lock;
    switcher_close(server, -1);
    overview_dismiss(server);
    add_listener(&wlr_lock->events.new_surface, &lock->new_surface, lock_new_surface);
    add_listener(&wlr_lock->events.unlock, &lock->unlock, lock_unlock);
    add_listener(&wlr_lock->events.destroy, &lock->destroy, lock_destroy);
    server->lock = lock;
    bool relock = server->locked;
    server->locked = true;
    if (server->grabbed_toplevel)
        reset_cursor_mode(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    wlr_seat_pointer_clear_focus(server->seat);
    wlr_scene_node_set_enabled(&server->lock_tree->node, true);
    wlr_log(WLR_INFO, "Session locked");
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        // A replacement locker for an abandoned lock finds the cover already shown.
        output->lock_presented = relock;
        wlr_output_schedule_frame(output->wlr_output);
    }
    send_locked_if_presented(server);
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
    struct sh_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct sh_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    free(inhibitor);
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, --server->inhibitors > 0);
}

#if WLR_HAS_SESSION
/* The machine stays awake while this VT is in front; switching away lets it sleep again. */
static void session_active(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, session_active);
    if (server->session->active && server->sleep_inhibitor < 0) {
        server->sleep_inhibitor = sh_sleep_inhibit();
    } else if (!server->session->active && server->sleep_inhibitor >= 0) {
        close(server->sleep_inhibitor);
        server->sleep_inhibitor = -1;
    }
}
#endif

/* Video players and games keep the session awake while any inhibitor exists. */
static void server_new_inhibitor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
    struct sh_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return;
    inhibitor->server = server;
    add_listener(&wlr_inhibitor->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, ++server->inhibitors > 0);
}

static bool output_named(const struct sh_output *output, const char *name) {
    return strcmp(output->wlr_output->name, name) == 0;
}

static bool output_listed(const struct sh_settings *settings, const struct sh_output *output) {
    for (int i = 0; i < settings->output_count; ++i) {
        if (output_named(output, settings->output_order[i]))
            return true;
    }
    return false;
}

/* "make model serial", which outputs.monitors can match with a "desc:" prefix. */
static void output_description(const struct wlr_output *output, char *text, size_t size) {
    snprintf(text, size, "%s %s %s", output->make ? output->make : "",
             output->model ? output->model : "", output->serial ? output->serial : "");
}

/* A "desc:" key matches the start of the output's description, as Hyprland's does. */
static bool output_key_matches(const char *key, const struct wlr_output *output) {
    if (strncmp(key, "desc:", 5) != 0)
        return strcmp(key, output->name) == 0;
    char description[256];
    output_description(output, description, sizeof(description));
    const char *prefix = key + 5;
    return *prefix && strncmp(description, prefix, strlen(prefix)) == 0;
}

static bool monitor_matches(const struct sh_monitor *monitor, const struct wlr_output *output) {
    return output_key_matches(monitor->name, output);
}

/* Settings by connector name win over a description match. */
static const struct sh_monitor *monitor_settings(const struct sh_settings *settings,
                                                 const struct wlr_output *output) {
    const struct sh_monitor *described = NULL;
    for (int i = 0; i < settings->monitor_count; ++i) {
        const struct sh_monitor *monitor = &settings->monitors[i];
        if (!monitor_matches(monitor, output))
            continue;
        if (strncmp(monitor->name, "desc:", 5) != 0)
            return monitor;
        described = described ? described : monitor;
    }
    return described;
}

/* The monitor settings in force for `output`: what an output-management client applied, else
 * what the configuration says. */
static const struct sh_monitor *output_monitor(const struct sh_settings *settings,
                                               const struct sh_output *output) {
    return output->has_override ? &output->override
                                : monitor_settings(settings, output->wlr_output);
}

/* Tells output-management clients how the outputs are set up now. */
static void publish_output_configuration(struct sh_server *server) {
    if (!server->output_manager)
        return;
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    if (!config)
        return;
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) {
            struct wlr_output_configuration_head_v1 *head =
                wlr_output_configuration_head_v1_create(config, output->wlr_output);
            if (!head)
                continue;
            head->state.enabled = !output->disabled && output->wlr_output->enabled;
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
            head->state.x = box.x;
            head->state.y = box.y;
        }
    }
    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

/* Adds the output to the layout at x, y, or moves it there. */
static void layout_output(struct sh_server *server, struct wlr_output *wlr_output, int x, int y) {
    bool present = wlr_output_layout_get(server->output_layout, wlr_output) != NULL;
    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add(server->output_layout, wlr_output, x, y);
    if (present || l_output == NULL)
        return;
    // Removing an output from the layout also destroys its scene output.
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server->scene, wlr_output);
    if (scene_output == NULL)
        scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

/* Moves the windows on `output` along with it, so they stay where they were on its screen.
 * X11 windows are told their new place too. */
static void follow_moved_output(struct sh_server *server, struct sh_output *output) {
    struct wlr_box now;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &now);
    int dx = now.x - output->previous.x, dy = now.y - output->previous.y;
    if (wlr_box_empty(&output->previous) || wlr_box_empty(&now) || (dx == 0 && dy == 0))
        return;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (strcmp(toplevel->output, output->wlr_output->name) != 0)
            continue;
        if (toplevel->restore_box.width > 0) {
            toplevel->restore_box.x += dx;
            toplevel->restore_box.y += dy;
        }
        if (toplevel->fullscreen_restore.width > 0) {
            toplevel->fullscreen_restore.x += dx;
            toplevel->fullscreen_restore.y += dy;
        }
        if (toplevel_mapped(toplevel))
            toplevel_set_position(toplevel, toplevel->scene_tree->node.x + dx,
                                  toplevel->scene_tree->node.y + dy);
    }
}

/* Outputs with a configured position go there. The rest sit side by side, top-aligned, to
 * the right of those: configured order first, then the order they appeared. Everything then
 * shifts so the layout starts at 0, 0: X11 has no place left of or above its root window,
 * so X11 windows on a monitor at negative coordinates would get no input. Windows, and
 * once running the pointer, move with their monitor; the pointer is put on the primary
 * output (if named) when it appears. */
static void arrange_outputs(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_output *output;
    int x = 0;
    bool positioned = false;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_monitor *monitor = output_monitor(settings, output);
        if (monitor == NULL || !monitor->positioned)
            continue;
        int width, height;
        wlr_output_effective_resolution(output->wlr_output, &width, &height);
        output->x = monitor->x;
        output->y = monitor->y;
        x = positioned && x > monitor->x + width ? x : monitor->x + width;
        positioned = true;
    }
    for (int i = 0; i <= settings->output_count; ++i) {
        wl_list_for_each_reverse(output, &server->outputs, link) {
            const struct sh_monitor *monitor = output_monitor(settings, output);
            if ((monitor != NULL && monitor->positioned) ||
                (i < settings->output_count ? !output_named(output, settings->output_order[i])
                                            : output_listed(settings, output)))
                continue;
            output->x = x;
            output->y = 0;
            int width, height;
            wlr_output_effective_resolution(output->wlr_output, &width, &height);
            x += width;
        }
    }
    int origin_x = INT_MAX, origin_y = INT_MAX;
    wl_list_for_each(output, &server->outputs, link) {
        origin_x = output->x < origin_x ? output->x : origin_x;
        origin_y = output->y < origin_y ? output->y : origin_y;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &output->previous);
    }
    struct wlr_output *pointed =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    struct wlr_box pointed_before = {0};
    if (pointed)
        wlr_output_layout_get_box(server->output_layout, pointed, &pointed_before);
    wl_list_for_each(output, &server->outputs, link)
        layout_output(server, output->wlr_output, output->x - origin_x, output->y - origin_y);
    wl_list_for_each(output, &server->outputs, link) follow_moved_output(server, output);
    struct wlr_output *primary = find_output(server, settings->primary_output);
    struct wlr_box box;
    if (primary && strcmp(server->placed_primary, settings->primary_output) != 0) {
        wlr_output_layout_get_box(server->output_layout, primary, &box);
        wlr_cursor_warp(server->cursor, NULL, box.x + box.width / 2.0, box.y + box.height / 2.0);
    } else if (server->running && pointed &&
               wlr_output_layout_get(server->output_layout, pointed)) {
        wlr_output_layout_get_box(server->output_layout, pointed, &box);
        wlr_cursor_warp(server->cursor, NULL, server->cursor->x + box.x - pointed_before.x,
                        server->cursor->y + box.y - pointed_before.y);
    }
    snprintf(server->placed_primary, sizeof(server->placed_primary), "%s",
             primary ? settings->primary_output : "");
    update_backgrounds(server);
    arrange_layers(server);
    refit_fullscreen(server);
    publish_output_configuration(server);
    notify_subscribers(server); // the list of outputs and their workspaces
}

/* The mode for `monitor`: its resolution at the refresh closest to the one asked for, or the
 * fastest there. Without settings, the preferred resolution at its fastest refresh, since
 * monitors often mark a 60 Hz mode as preferred. NULL when nothing matches. */
static struct wlr_output_mode *pick_mode(struct wlr_output *wlr_output,
                                         const struct sh_monitor *monitor) {
    struct wlr_output_mode *preferred = wlr_output_preferred_mode(wlr_output);
    int width = monitor && monitor->width ? monitor->width : preferred ? preferred->width : 0;
    int height = monitor && monitor->width ? monitor->height : preferred ? preferred->height : 0;
    int refresh = monitor ? monitor->refresh : 0;
    struct wlr_output_mode *best = NULL, *candidate;
    wl_list_for_each(candidate, &wlr_output->modes, link) {
        if (candidate->width != width || candidate->height != height)
            continue;
        if (best == NULL ||
            (refresh ? abs(candidate->refresh - refresh) < abs(best->refresh - refresh)
                     : candidate->refresh > best->refresh))
            best = candidate;
    }
    return best;
}

static void destroy_output_layers(struct sh_server *server, struct wlr_output *wlr_output) {
    struct sh_layer *layer, *temporary;
    wl_list_for_each_safe(layer, temporary, &server->layers, link) {
        if (layer->surface->output == wlr_output)
            wlr_layer_surface_v1_destroy(layer->surface);
    }
}

/* Applies outputs.monitors (or the defaults) to one output and files it under the enabled or
 * disabled list. Only settings that differ are committed, so a reload does not modeset
 * needlessly. The last enabled output stays on. Callers arrange the outputs afterwards. */
static void configure_output(struct sh_server *server, struct sh_output *output) {
    struct wlr_output *wlr_output = output->wlr_output;
    const struct sh_monitor *monitor = output_monitor(server_settings(server), output);
    bool enable = monitor == NULL || monitor->enabled;
    if (!enable) {
        bool others = false;
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) others |= candidate != output;
        if (!others) {
            wlr_log(WLR_ERROR, "Keeping %s on: it is the only output", wlr_output->name);
            enable = true;
        }
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!enable) {
        wlr_output_state_set_enabled(&state, false);
    } else {
        if (!wlr_output->enabled)
            wlr_output_state_set_enabled(&state, true);
        struct wlr_output_mode *mode = pick_mode(wlr_output, monitor);
        if (mode != NULL && mode != wlr_output->current_mode) {
            wlr_output_state_set_mode(&state, mode);
        } else if (mode == NULL && monitor != NULL && monitor->width != 0) {
            if (wl_list_empty(&wlr_output->modes))
                wlr_output_state_set_custom_mode(&state, monitor->width, monitor->height,
                                                 monitor->refresh);
            else
                wlr_log(WLR_ERROR, "%s has no %dx%d mode", wlr_output->name, monitor->width,
                        monitor->height);
        }
        float scale = monitor && monitor->scale > 0 ? monitor->scale : 1;
        if (scale != wlr_output->scale)
            wlr_output_state_set_scale(&state, scale);
        enum wl_output_transform transform = monitor ? monitor->transform : 0;
        if (transform != wlr_output->transform)
            wlr_output_state_set_transform(&state, transform);
        // Outputs that cannot switch it (nested ones report it on) are left alone.
        bool vrr = monitor && monitor->vrr;
        if (wlr_output->adaptive_sync_supported &&
            vrr != (wlr_output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED))
            wlr_output_state_set_adaptive_sync_enabled(&state, vrr);
    }
    if (state.committed != 0 && !wlr_output_test_state(wlr_output, &state)) {
        // Fall back to the defaults; a new monitor must still light up.
        wlr_log(WLR_ERROR, "%s rejected its configured settings", wlr_output->name);
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        struct wlr_output_mode *mode = pick_mode(wlr_output, NULL);
        if (mode != NULL)
            wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_scale(&state, 1);
        wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);
        if (!wlr_output_test_state(wlr_output, &state))
            wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));
        enable = true;
    }
    if (state.committed != 0)
        wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    // The windows of an output that is turned off go to another, as if it were unplugged.
    bool turned_off = !enable && !wl_list_empty(&output->link) && !output->disabled;
    struct wlr_box gone = {0};
    if (turned_off) {
        gone = output->usable;
        if (wlr_box_empty(&gone))
            wlr_output_layout_get_box(server->output_layout, wlr_output, &gone);
    }
    if (wl_list_empty(&output->link) || enable == output->disabled) {
        wl_list_remove(&output->link);
        wl_list_insert(enable ? &server->outputs : &server->disabled_outputs, &output->link);
        output->disabled = !enable;
    }
    if (enable) {
        wlr_scene_node_set_enabled(&output->background->node, true);
        wlr_scene_node_set_enabled(&output->lock_blank->node, true);
    } else {
        destroy_output_layers(server, wlr_output);
        wlr_output_layout_remove(server->output_layout, wlr_output);
        wlr_scene_node_set_enabled(&output->background->node, false);
        wlr_scene_node_set_enabled(&output->lock_blank->node, false);
        if (turned_off && server->running)
            evacuate_output(server, wlr_output->name, gone, false);
    }
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

static struct sh_output *sh_output_for(struct sh_server *server, struct wlr_output *wlr_output) {
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) {
            if (output->wlr_output == wlr_output)
                return output;
        }
    }
    return NULL;
}

/* Settings a wlr-output-management head asks for, as the monitor entry they amount to. */
static void head_monitor(struct sh_server *server, const struct sh_output *output,
                         const struct wlr_output_head_v1_state *head, struct sh_monitor *monitor) {
    const struct sh_monitor *configured = monitor_settings(server_settings(server), output->wlr_output);
    memset(monitor, 0, sizeof(*monitor));
    monitor->tiling = configured ? configured->tiling : -1;
    snprintf(monitor->name, sizeof(monitor->name), "%s", output->wlr_output->name);
    monitor->enabled = head->enabled;
    if (!head->enabled)
        return;
    if (head->mode) {
        monitor->width = head->mode->width;
        monitor->height = head->mode->height;
        monitor->refresh = head->mode->refresh;
    } else {
        monitor->width = head->custom_mode.width;
        monitor->height = head->custom_mode.height;
        monitor->refresh = head->custom_mode.refresh;
    }
    monitor->scale = head->scale;
    monitor->transform = head->transform;
    monitor->vrr = head->adaptive_sync_enabled;
    monitor->positioned = true;
    monitor->x = head->x;
    monitor->y = head->y;
}

static bool test_head(const struct wlr_output_head_v1_state *head) {
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, head->enabled);
    if (head->enabled) {
        if (head->mode)
            wlr_output_state_set_mode(&state, head->mode);
        else if (head->custom_mode.width > 0 && head->custom_mode.height > 0)
            wlr_output_state_set_custom_mode(&state, head->custom_mode.width,
                                             head->custom_mode.height, head->custom_mode.refresh);
        wlr_output_state_set_scale(&state, head->scale);
        wlr_output_state_set_transform(&state, head->transform);
    }
    bool ok = head->scale >= 0 && wlr_output_test_state(head->output, &state);
    wlr_output_state_finish(&state);
    return ok;
}

/* A request is acceptable when every head is known and its settings pass the backend's test,
 * and an output stays on. */
static bool output_configuration_ok(struct sh_server *server,
                                    struct wlr_output_configuration_v1 *config) {
    bool any_on = false;
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        if (!sh_output_for(server, head->state.output) || !test_head(&head->state))
            return false;
        any_on |= head->state.enabled;
    }
    // Outputs the request leaves out keep their state.
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        bool listed = false;
        wl_list_for_each(head, &config->heads, link) listed |= head->state.output == output->wlr_output;
        any_on |= !listed;
    }
    return any_on;
}

static void output_config_test(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_test);
    struct wlr_output_configuration_v1 *config = data;
    if (output_configuration_ok(server, config))
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
}

static void output_config_apply(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_apply);
    struct wlr_output_configuration_v1 *config = data;
    if (!output_configuration_ok(server, config)) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        struct sh_output *output = sh_output_for(server, head->state.output);
        head_monitor(server, output, &head->state, &output->override);
        output->has_override = true;
    }
    // Enable outputs before disabling others, so a swap never leaves none on.
    struct sh_output *output, *temporary;
    wl_list_for_each_safe(output, temporary, &server->disabled_outputs, link)
        configure_output(server, output);
    wl_list_for_each_safe(output, temporary, &server->outputs, link)
        configure_output(server, output);
    arrange_outputs(server);
    reconfigure_tiling(server);
    return_home_windows(server);
    rehome_tiles(server);
    show_workspaces(server);
    struct sh_toplevel *toplevel;
    if (server->focused_toplevel && !toplevel_visible(server->focused_toplevel)) {
        deactivate_toplevel(server);
        focus_previous(server);
    }
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    if (wlr_output_commit_state(output->wlr_output, event->state))
        arrange_outputs(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    output_release_zoom(output);
    destroy_output_layers(output->server, output->wlr_output);
    wlr_scene_node_destroy(&output->background->node);
    wlr_scene_node_destroy(&output->lock_blank->node);
    struct sh_server *server = output->server;
    if (!strcmp(server->overview.output, output->wlr_output->name))
        overview_dismiss(server);
    if (server->running)
        schedule_evacuation(server, output);
    // Closing the host window ends a nested session. A standalone session loses every output
    // on VT switch (wlroots recreates them on return) or when the last monitor is unplugged.
    bool standalone = false;
#if WLR_HAS_SESSION
    standalone = server->session != NULL;
#endif
    if (server->running && !standalone && wl_list_empty(&server->outputs))
        wl_display_terminate(server->wl_display);
    free(output);
    if (server->running)
        arrange_outputs(server);
    send_locked_if_presented(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct sh_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    output->background =
        wlr_scene_rect_create(server->backgrounds, 1, 1, server_settings(server)->background);
    static const float lock_color[4] = {0, 0, 0, 1};
    // lock_presented starts false: an output added while locked must not show the desktop.
    output->lock_blank = wlr_scene_rect_create(server->lock_blanks, 1, 1, lock_color);
    add_listener(&wlr_output->events.frame, &output->frame, output_frame);
    add_listener(&wlr_output->events.request_state, &output->request_state, output_request_state);
    add_listener(&wlr_output->events.destroy, &output->destroy, output_destroy);

    if (server->pending_output_name[0]) {
        wlr_output_set_name(wlr_output, server->pending_output_name);
        server->pending_output_name[0] = '\0';
    }
    wl_list_init(&output->link);
    char description[256];
    output_description(wlr_output, description, sizeof(description));
    wlr_log(WLR_INFO, "Output %s: %s", wlr_output->name, description);
    apply_output_layout(server, wlr_output);
    configure_output(server, output);
    if (wlr_output_is_wl(wlr_output))
        wlr_wl_output_set_title(wlr_output, "shaodesk — nested desktop");
    arrange_outputs(server);
    return_home_windows(server);
}

/* Layouts put their gap at the edges as well as between windows. Laying out with gap_inner
 * in an area grown or shrunk by the difference leaves gap_outer at the edges. Maximized
 * windows ignore gaps. */
static struct sh_rect gap_area(const struct sh_settings *settings, struct sh_rect area,
                               enum sh_action action) {
    if (action == SH_MAXIMIZE)
        return area;
    int d = settings->gap_outer - settings->gap_inner;
    return (struct sh_rect){area.x + d, area.y + d, area.width - 2 * d, area.height - 2 * d};
}

/* Fullscreen windows have no border, nor do maximized ones on an output that does not tile:
 * their top edge is the screen's, so the pointer pushed against it lands on the window's drag
 * strip rather than a border. */
static bool frameless(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return toplevel->fullscreen ||
           (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE && !toplevel->tiled &&
            !output_tiles(toplevel->server, output));
}

/* Placed windows keep their border inside their slot. */
static struct sh_rect inside_border(struct sh_server *server, struct sh_rect rect) {
    int b = server_settings(server)->border_width;
    if (rect.width <= 2 * b || rect.height <= 2 * b)
        return rect;
    return (struct sh_rect){rect.x + b, rect.y + b, rect.width - 2 * b, rect.height - 2 * b};
}

/* Preserve the original floating rectangle across repeated snap operations. */
static void place_toplevel(struct sh_toplevel *toplevel, enum sh_action action,
                           struct sh_rect target) {
    if (!toplevel->arranged)
        toplevel->restore_box = toplevel_box(toplevel);
    toplevel->arranged = true;
    toplevel->arrangement = action;
    struct wlr_box box = {target.x, target.y, target.width, target.height};
    if (!frameless(toplevel, box_output(toplevel->server, box)))
        target = inside_border(toplevel->server, target);
    toplevel_set_states(toplevel, action == SH_MAXIMIZE, action == SH_MAXIMIZE ? 0 : ALL_EDGES);
    toplevel_configure(toplevel, target.x, target.y, target.width, target.height);
}

/* The output a box shares the most area with, or NULL when it is on none. */
static struct wlr_output *box_output(struct sh_server *server, struct wlr_box box) {
    struct wlr_output *best = NULL;
    long best_area = 0;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box full, shared;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
        if (!wlr_box_intersection(&shared, &box, &full))
            continue;
        long area = (long)shared.width * shared.height;
        if (area > best_area) {
            best = output->wlr_output;
            best_area = area;
        }
    }
    return best;
}

/* A tile belongs to the output whose tiling holds it. Any other window belongs to the output
 * it covers most, not the one under its top-left corner: a window dropped across two
 * outputs, or moved from a larger one, must fit the output it is mostly on. */
static struct wlr_output *toplevel_output(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = toplevel->tiled ? tiled_output(toplevel) : NULL;
    struct wlr_box box = toplevel_box(toplevel);
    if (!output)
        output = box_output(server, box);
    if (!output) {
        // Off every output: the one nearest its centre.
        double x, y;
        wlr_output_layout_closest_point(server->output_layout, NULL, box.x + box.width / 2.0,
                                        box.y + box.height / 2.0, &x, &y);
        output = wlr_output_layout_output_at(server->output_layout, x, y);
    }
    return output ? output : first_output(server);
}

/* A saved floating box, moved onto `output` when it was saved on another one: the same
 * place relative to the usable area, kept inside it. A box already on `output` only moves
 * if it sticks out past the edge. */
static struct wlr_box rebase_box(struct sh_server *server, struct wlr_box box,
                                 struct wlr_output *output) {
    if (!output || box.width <= 0 || box.height <= 0)
        return box;
    struct wlr_output *from = box_output(server, box);
    struct wlr_box full, shared;
    wlr_output_layout_get_box(server->output_layout, output, &full);
    if (from == output && wlr_box_intersection(&shared, &box, &full) &&
        wlr_box_equal(&shared, &box))
        return box;
    struct sh_rect area = usable_area(server, output);
    if (from && from != output) {
        struct sh_rect old = usable_area(server, from);
        box.x += area.x - old.x;
        box.y += area.y - old.y;
    }
    box.width = fmin(box.width, area.width);
    box.height = fmin(box.height, area.height);
    box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    return box;
}

static void restore_toplevel(struct sh_toplevel *toplevel) {
    if (!toplevel->arranged)
        return;
    struct wlr_output *output = toplevel_output(toplevel);
    toplevel->arranged = false;
    toplevel_set_states(toplevel, false, 0);
    toplevel->restore_box = rebase_box(toplevel->server, toplevel->restore_box, output);
    toplevel_configure_box(toplevel, toplevel->restore_box);
}

static void place_maximized(struct sh_toplevel *toplevel) {
    struct wlr_output *output = toplevel_output(toplevel);
    if (output)
        place_toplevel(toplevel, SH_MAXIMIZE, usable_area(toplevel->server, output));
}

/* Windows taking part in the one-shot grid arrangement of `output`. */
static bool in_grid(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return toplevel_visible(toplevel) && !toplevel->fullscreen &&
           toplevel_output(toplevel) == output;
}

/* Snaps or maximizes one window within the usable area of its output. A tiled window placed
 * by hand floats from then on, or, lifted out by a drag, stays out of the tiling. Snapping a
 * window again to the side it is on moves it to the near half of the next output that way. */
static void place_by_hand(struct sh_toplevel *toplevel, enum sh_action action) {
    struct sh_server *server = toplevel->server;
    bool again = (action == SH_SNAP_LEFT || action == SH_SNAP_RIGHT) && toplevel->arranged &&
                 !toplevel->tiled && toplevel->arrangement == action;
    if (toplevel->tiled || wants_tiling(toplevel, NULL))
        toplevel->floating = toplevel->placed = true;
    if (toplevel->tiled)
        untile_toplevel(toplevel, false);
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    if (again) {
        struct wlr_box box = toplevel_box(toplevel);
        struct wlr_output *next = wlr_output_layout_adjacent_output(
            server->output_layout, action == SH_SNAP_LEFT ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT,
            output, box.x + box.width / 2.0, box.y + box.height / 2.0);
        if (next) {
            output = next;
            action = action == SH_SNAP_LEFT ? SH_SNAP_RIGHT : SH_SNAP_LEFT;
        }
    }
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), action), target;
    if (sh_placement(action, area, settings->gap_inner, 0, 1, &target))
        place_toplevel(toplevel, action, target);
}

static void arrange_windows(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *focused = current_toplevel(server);
    if (!focused)
        return;
    if (action == SH_TILE && output_tiles(server, toplevel_output(focused)))
        return; // Already tiled automatically.
    if (focused->tiled && action == SH_RESTORE)
        return;
    if (focused->fullscreen)
        set_fullscreen(focused, false);
    if (action == SH_RESTORE) {
        restore_toplevel(focused);
        return;
    }
    if (action != SH_TILE) {
        place_by_hand(focused, action);
        return;
    }
    struct wlr_output *output = toplevel_output(focused);
    if (!output)
        return;
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), action), target;
    int gap = settings->gap_inner;
    int count = 0, index = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) count += in_grid(toplevel, output);
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (in_grid(toplevel, output) && sh_placement(action, area, gap, index++, count, &target))
            place_toplevel(toplevel, action, target);
    }
}

static void place_tiled(void *data, void *window, struct sh_rect rect) {
    struct sh_toplevel *toplevel = window;
    if (toplevel->fullscreen)
        return; // It returns to its tile when it leaves fullscreen.
    toplevel_set_states(toplevel, false, ALL_EDGES);
    rect = inside_border(toplevel->server, rect);
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    int x = node->x, y = node->y;
    toplevel_configure(toplevel, rect.x, rect.y, rect.width, rect.height);
    // Tiles already on screen glide to their new place; the size follows when the client
    // draws it.
    if (toplevel->shown && toplevel_visible(toplevel))
        sh_anim_glide(toplevel->server->animator, &toplevel->anim, toplevel->content, x - node->x,
                      y - node->y);
}

/* Snapped, maximized, or grid-arranged windows that follow changes to the usable area. */
static bool reflows(struct sh_toplevel *toplevel, int workspace, struct wlr_output *output) {
    return toplevel->workspace == workspace && toplevel->arranged && !toplevel->minimized &&
           !toplevel->group_hidden && !toplevel->swallowed &&
           !toplevel->fullscreen && toplevel_output(toplevel) == output;
}

static void reflow_output(struct sh_server *server, struct wlr_output *output) {
    if (server->reflow_held)
        return;
    ++server->stats.reflows;
    uint64_t started = now_ns();
    struct sh_rect area = usable_area(server, output), target;
    const struct sh_settings *settings = server_settings(server);
    for (int workspace = 0; workspace < settings->workspaces; ++workspace) {
        int count = 0, index = 0;
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            count += reflows(toplevel, workspace, output) && toplevel->arrangement == SH_TILE;
        }
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (!reflows(toplevel, workspace, output))
                continue;
            enum sh_action action = toplevel->arrangement;
            if (sh_placement(action, gap_area(settings, area, action), settings->gap_inner,
                             action == SH_TILE ? index++ : 0, action == SH_TILE ? count : 1,
                             &target))
                place_toplevel(toplevel, action, target);
        }
        sh_tiling_arrange(server->tiling, output->name, workspace,
                          gap_area(settings, area, SH_TILE), settings->gap_inner, place_tiled,
                          NULL);
    }
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen && toplevel_output(toplevel) == output)
            fit_fullscreen(toplevel); // it follows the panels' exclusive zones
    }
    server->stats.reflow_ns += now_ns() - started;
}

static struct wlr_output *find_output(struct sh_server *server, const char *name) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output_named(output, name))
            return output->wlr_output;
    }
    return NULL;
}

static struct wlr_output *tiled_output(struct sh_toplevel *toplevel) {
    const char *name = sh_tiling_output(toplevel->server->tiling, toplevel);
    return name ? find_output(toplevel->server, name) : NULL;
}

static enum sh_tile_layout toplevel_layout(struct sh_toplevel *toplevel) {
    const char *name = sh_tiling_output(toplevel->server->tiling, toplevel);
    return name ? sh_tiling_layout(toplevel->server->tiling, name, toplevel->workspace)
                : SH_LAYOUT_DWINDLE;
}

/* The tiling setting the config gives `output`: its own, else layout.tiling. */
static bool configured_tiling(struct sh_server *server, struct wlr_output *output) {
    const struct sh_settings *settings = server_settings(server);
    const struct sh_monitor *monitor = monitor_settings(settings, output);
    return monitor && monitor->tiling >= 0 ? monitor->tiling : settings->tiling;
}

/* Whether `output` tiles its windows automatically: as configured, until it is toggled. */
static bool output_tiles(struct sh_server *server, struct wlr_output *output) {
    if (!output)
        return false;
    int slot = output_slot(server, output->name);
    if (server->output_workspaces[slot].tiling < 0) {
        bool configured = configured_tiling(server, output);
        server->output_workspaces[slot].tiling = configured;
        server->output_workspaces[slot].configured = configured;
    }
    return server->output_workspaces[slot].tiling;
}

/* The output whose tiling an untiled window joins: the one it was placed on, else the one it
 * is on. */
static struct wlr_output *home_output(struct sh_toplevel *toplevel) {
    struct wlr_output *output = find_output(toplevel->server, toplevel->output);
    return output ? output : toplevel_output(toplevel);
}

/* Whether the window should join the tiling of `output` (by default its home output). */
static bool wants_tiling(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return !toplevel->tiled && !toplevel->group_hidden && !toplevel->swallowed && !toplevel->floating && !toplevel->sticky && !toplevel->minimized &&
           output_tiles(toplevel->server, output ? output : home_output(toplevel));
}

/* Dialogs and fixed-size windows float, as in Hyprland. */
static bool toplevel_is_dialog(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        const struct wlr_xwayland_surface *xsurface = toplevel->xsurface;
        const xcb_size_hints_t *hints = xsurface->size_hints;
        enum wlr_xwayland_net_wm_window_type types[] = {WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG,
                                                        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY,
                                                        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH};
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
            if (wlr_xwayland_surface_has_window_type(xsurface, types[i]))
                return true;
        // Like xdg-shell below: a window of one fixed size cannot fill a tile.
        uint32_t fixed = XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
        return xsurface->parent || xsurface->modal ||
               (hints && (hints->flags & fixed) == fixed && hints->min_width > 0 &&
                hints->min_width == hints->max_width && hints->min_height > 0 &&
                hints->min_height == hints->max_height);
    }
#endif
    const struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
    return toplevel->xdg_toplevel->parent ||
           (state->min_width > 0 && state->min_width == state->max_width && state->min_height > 0 &&
            state->min_height == state->max_height);
}

/* Adds a window to the tiling of `output` (by default the one it is on), splitting `target`
 * when that is tiled there, else the tile under the point x, y with `has_point`. */
static void tile_toplevel_at(struct sh_toplevel *toplevel, struct wlr_output *output,
                             struct sh_toplevel *target, bool has_point, double x, double y) {
    struct sh_server *server = toplevel->server;
    if (!output)
        output = home_output(toplevel);
    if (!output || toplevel->tiled)
        return;
    set_toplevel_output(toplevel, output);
    if (!toplevel->arranged)
        toplevel->restore_box =
            toplevel->fullscreen ? toplevel->fullscreen_restore : toplevel_box(toplevel);
    if (toplevel->tile_sized) // Floating later lets the client choose its size.
        toplevel->restore_box.width = toplevel->restore_box.height = 0;
    toplevel->tile_sized = false;
    toplevel->arranged = toplevel->placed = false;
    toplevel->tiled = true;
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign, false);
    sh_tiling_insert(server->tiling, output->name, toplevel->workspace, toplevel, target, has_point,
                     x, y);
    reflow_output(server, output);
}

/* As tile_toplevel_at, at the tile under the pointer with `at_cursor`. */
static void tile_toplevel(struct sh_toplevel *toplevel, struct wlr_output *output,
                          struct sh_toplevel *target, bool at_cursor) {
    struct wlr_cursor *cursor = toplevel->server->cursor;
    tile_toplevel_at(toplevel, output, target, at_cursor, cursor->x, cursor->y);
}

/* Takes a window out of the tiling. `restore` returns it to its floating geometry; otherwise
 * it stays where it is, arranged without a rule, until something else places it. */
static void untile_toplevel(struct sh_toplevel *toplevel, bool restore) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->tiled)
        return;
    struct wlr_output *output = tiled_output(toplevel);
    sh_tiling_remove(server->tiling, toplevel);
    toplevel->tiled = false;
    toplevel->arranged = !restore;
    toplevel->arrangement = SH_NONE;
    // The floating geometry was saved where the window entered the tiling, maybe elsewhere.
    // A window that opened tiled has no floating size: it floats where its tile is now.
    if (restore && (toplevel->restore_box.width <= 0 || toplevel->restore_box.height <= 0)) {
        struct wlr_box tile = toplevel_box(toplevel);
        toplevel->restore_box.x = tile.x;
        toplevel->restore_box.y = tile.y;
    } else if (restore) {
        toplevel->restore_box = rebase_box(server, toplevel->restore_box, output);
    }
    if (restore && toplevel->fullscreen) {
        toplevel->fullscreen_restore = toplevel->restore_box;
    } else if (restore) {
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure_box(toplevel, toplevel->restore_box);
    }
    if (output && output_tiles(server, output))
        reflow_output(server, output);
}

/* `box` moved from where it was on the output covering `from` to the same place relative to
 * `area`, kept inside it. Sizes stay unless they no longer fit. */
static struct wlr_box translate_box(struct wlr_box box, struct wlr_box from, struct sh_rect area) {
    if (box.width <= 0 || box.height <= 0 || from.width <= 0 || from.height <= 0)
        return box;
    double rx = (double)(box.x - from.x) / from.width, ry = (double)(box.y - from.y) / from.height;
    box.width = fmin(box.width, area.width);
    box.height = fmin(box.height, area.height);
    box.x = area.x + (int)lround(rx * area.width);
    box.y = area.y + (int)lround(ry * area.height);
    box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    return box;
}

/* The connected output nearest the box, or NULL when there is none. */
static struct wlr_output *nearest_output(struct sh_server *server, struct wlr_box box) {
    struct wlr_output *best = NULL;
    double best_distance = 0;
    struct sh_output *candidate;
    wl_list_for_each(candidate, &server->outputs, link) {
        struct wlr_box other;
        wlr_output_layout_get_box(server->output_layout, candidate->wlr_output, &other);
        double dx = (other.x + other.width / 2.0) - (box.x + box.width / 2.0);
        double dy = (other.y + other.height / 2.0) - (box.y + box.height / 2.0);
        double distance = dx * dx + dy * dy;
        if (!best || distance < best_distance) {
            best = candidate->wlr_output;
            best_distance = distance;
        }
    }
    return best;
}

/* Puts a floating window at `box`, or a fullscreen one over `output`, so that the
 * output the window follows is the one it was sent to. */
static void place_on_output(struct sh_toplevel *toplevel, struct wlr_output *output,
                            struct wlr_box box) {
    if (toplevel->fullscreen)
        box = fullscreen_box(toplevel, output);
    toplevel_configure_box(toplevel, box);
}

/* Moves a window from `from` (the box of the output it is on) onto `to`, keeping its workspace
 * number and, on an output that tiles, its place in the tiling. */
static void relocate_toplevel(struct sh_toplevel *toplevel, struct wlr_box from,
                              struct wlr_output *to, bool tile, bool keep_workspace) {
    struct sh_server *server = toplevel->server;
    struct sh_rect area = usable_area(server, to);
    int workspace = toplevel->workspace;
    toplevel->restore_box = translate_box(toplevel->restore_box, from, area);
    toplevel->fullscreen_restore = translate_box(toplevel->fullscreen_restore, from, area);
    struct wlr_box box = translate_box(toplevel_box(toplevel), from, area);
    bool was_tiled = toplevel->tiled;
    if (was_tiled)
        untile_toplevel(toplevel, false);
    set_toplevel_output(toplevel, to); // joins the workspace `to` shows
    if (keep_workspace)
        toplevel->workspace = workspace;
    group_follow(toplevel);
    if (was_tiled && tile && wants_tiling(toplevel, to)) {
        tile_toplevel_at(toplevel, to, NULL, false, 0, 0);
    } else if (was_tiled) {
        restore_toplevel(toplevel); // floats where it was
    } else if (toplevel_mapped(toplevel)) {
        place_on_output(toplevel, to, box);
    }
}

/* An output is going away: its windows move to the nearest one that is left, keeping their
 * workspace numbers, and remember where they came from so they can return with it. Without
 * another output they stay as they are, as they do when a VT switch takes every output. */
static void evacuate_output(struct sh_server *server, const char *name, struct wlr_box gone,
                            bool keep_workspaces) {
    if (wlr_box_empty(&gone) || find_output(server, name))
        return;
    struct wlr_output *target = nearest_output(server, gone);
    if (!target)
        return;
    bool remember = server_settings(server)->return_windows;
    size_t count = 0, capacity = 0;
    struct sh_toplevel **moving = NULL, *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (strcmp(toplevel->output, name) != 0)
            continue;
        if (count == capacity) {
            struct sh_toplevel **grown = realloc(moving, (capacity = capacity ? 2 * capacity : 16) * sizeof(*moving));
            if (!grown)
                break;
            moving = grown;
        }
        moving[count++] = toplevel;
    }
    if (count == 0) {
        free(moving);
        return;
    }
    ++server->reflow_held;
    for (size_t i = 0; i < count; ++i) {
        toplevel = moving[i];
        if (remember && !toplevel->home_output[0]) {
            snprintf(toplevel->home_output, sizeof(toplevel->home_output), "%s", name);
            toplevel->home_workspace = toplevel->workspace;
            toplevel->home_tiled = toplevel->tiled;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        toplevel = moving[i];
        // Hidden members of a group follow the one that shows.
        if (toplevel->group_hidden) {
            struct sh_rect area = usable_area(server, target);
            toplevel->restore_box = translate_box(toplevel->restore_box, gone, area);
            continue;
        }
        char home[64];
        int home_workspace = toplevel->home_workspace;
        bool home_tiled = toplevel->home_tiled;
        snprintf(home, sizeof(home), "%s", toplevel->home_output);
        relocate_toplevel(toplevel, gone, target, true, keep_workspaces);
        // Relocating counts as placing by hand; put the note back.
        snprintf(toplevel->home_output, sizeof(toplevel->home_output), "%s", home);
        toplevel->home_workspace = home_workspace;
        toplevel->home_tiled = home_tiled;
    }
    --server->reflow_held;
    free(moving);
    wlr_log(WLR_INFO, "Moved windows of %s to %s", name, target->name);
    show_workspaces(server);
    reflow_output(server, target);
    refit_fullscreen(server);
    if (server->focused_toplevel && !toplevel_visible(server->focused_toplevel)) {
        deactivate_toplevel(server);
        focus_previous(server);
    }
}

struct sh_evacuation {
    struct sh_server *server;
    char name[64];
    struct wlr_box box;
};

static void evacuation_run(void *data) {
    struct sh_evacuation *job = data;
    if (job->server->running)
        evacuate_output(job->server, job->name, job->box, true);
    free(job);
}

/* Called while the output is being destroyed, when the scene and the layout still hold it and
 * moving windows would touch it again: the move waits until it is gone. */
static void schedule_evacuation(struct sh_server *server, struct sh_output *output) {
    struct sh_evacuation *job = calloc(1, sizeof(*job));
    if (!job)
        return;
    job->server = server;
    snprintf(job->name, sizeof(job->name), "%s", output->wlr_output->name);
    job->box = output->usable; // windows keep their place relative to the area they could use
    if (wlr_box_empty(&job->box))
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &job->box);
    if (wlr_box_empty(&job->box))
        job->box = output->previous;
    wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display), evacuation_run, job);
}

/* Windows that came from an output that is connected again go back to it, to their old
 * workspace and, when they were tiled, into its tiling. */
static void return_home_windows(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    bool any = false;
    ++server->reflow_held;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (!toplevel->home_output[0])
            continue;
        if (!settings->return_windows || toplevel->group_hidden) {
            toplevel->home_output[0] = '\0';
            continue;
        }
        struct wlr_output *home = find_output(server, toplevel->home_output);
        if (!home)
            continue; // still away
        if (!strcmp(toplevel->output, toplevel->home_output)) {
            toplevel->home_output[0] = '\0';
            continue;
        }
        struct wlr_output *from = find_output(server, toplevel->output);
        struct wlr_box from_box = {0};
        if (from) {
            struct sh_rect area = usable_area(server, from);
            from_box = (struct wlr_box){area.x, area.y, area.width, area.height};
        } else {
            from_box = toplevel_box(toplevel);
        }
        int workspace = toplevel->home_workspace;
        bool tile = toplevel->home_tiled;
        toplevel->home_output[0] = '\0';
        // relocate_toplevel keeps the workspace number the window has now.
        toplevel->workspace = workspace;
        relocate_toplevel(toplevel, from_box, home, tile, true);
        any = true;
    }
    --server->reflow_held;
    if (!any)
        return;
    show_workspaces(server);
    // The outputs the windows left lose tiles too.
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
    refit_fullscreen(server);
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
}

/* The output an output-target words name, seen from `from`: "left" or "right" is the next
 * output that way, "next" or "prev" the one after or before it from left to right, wrapping;
 * anything else a connector name or "desc:" description. NULL when there is none, or it is
 * `from` itself. */
static struct wlr_output *resolve_output_target(struct sh_server *server, struct wlr_output *from,
                                                const char *target) {
    struct wlr_output *found = NULL;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, from, &box);
    if (!strcmp(target, "left") || !strcmp(target, "right")) {
        found = wlr_output_layout_adjacent_output(
            server->output_layout, !strcmp(target, "left") ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT,
            from, box.x + box.width / 2.0, box.y + box.height / 2.0);
    } else if (!strcmp(target, "next") || !strcmp(target, "prev")) {
        // The output whose left edge follows (or precedes) ours, else the far end.
        struct wlr_output *best = NULL, *edge = NULL;
        struct wlr_box best_box = {0}, edge_box = {0};
        bool forward = !strcmp(target, "next");
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) {
            if (candidate->wlr_output == from)
                continue;
            struct wlr_box other;
            wlr_output_layout_get_box(server->output_layout, candidate->wlr_output, &other);
            bool after = other.x > box.x || (other.x == box.x && other.y > box.y);
            bool nearer = !best || (forward ? (other.x < best_box.x ||
                                              (other.x == best_box.x && other.y < best_box.y))
                                            : (other.x > best_box.x ||
                                               (other.x == best_box.x && other.y > best_box.y)));
            if (after == forward && nearer) {
                best = candidate->wlr_output;
                best_box = other;
            }
            bool further = !edge || (forward ? (other.x < edge_box.x ||
                                               (other.x == edge_box.x && other.y < edge_box.y))
                                             : (other.x > edge_box.x ||
                                                (other.x == edge_box.x && other.y > edge_box.y)));
            if (further) {
                edge = candidate->wlr_output;
                edge_box = other;
            }
        }
        found = best ? best : edge;
    } else {
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) {
            if (candidate->wlr_output != from && output_key_matches(target, candidate->wlr_output)) {
                found = candidate->wlr_output;
                break;
            }
        }
    }
    return found != from ? found : NULL;
}

/* The windows a workspace exchange moves, with where each was drawn, for the glide. */
struct sh_exchange {
    struct sh_toplevel **windows;
    int *x, *y;
    size_t count, capacity;
};

static void exchange_note(struct sh_exchange *exchange, struct sh_toplevel *toplevel) {
    if (exchange->count == exchange->capacity) {
        size_t capacity = exchange->capacity ? 2 * exchange->capacity : 32;
        struct sh_toplevel **windows = realloc(exchange->windows, capacity * sizeof(*windows));
        int *x = realloc(exchange->x, capacity * sizeof(*x));
        int *y = realloc(exchange->y, capacity * sizeof(*y));
        if (windows)
            exchange->windows = windows;
        if (x)
            exchange->x = x;
        if (y)
            exchange->y = y;
        if (!windows || !x || !y)
            return;
        exchange->capacity = capacity;
    }
    exchange->windows[exchange->count] = toplevel;
    exchange->x[exchange->count] = toplevel->scene_tree->node.x;
    exchange->y[exchange->count++] = toplevel->scene_tree->node.y;
}

/* Whether a window goes along with its workspace: sticky windows and the scratchpad's belong
 * to their output. */
static bool travels_with_workspace(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return false;
#endif
    return toplevel->output[0] && !toplevel->sticky && !toplevel->scratchpad;
}

/* Trades what workspace `wa` of output `a` and workspace `wb` of `b` hold: their windows, moved
 * to the other output (a floating window keeps its place relative to the usable area, a tile its
 * place in the tiling, which comes along with its layout, ratio and columns), and their layout
 * state. A tile that lands on an output that does not tile floats, and a floating window that
 * lands on one that does joins the tiling. Nothing is shown or arranged here. */
static void exchange_workspace_slots(struct sh_server *server, struct wlr_output *a, int wa,
                                     struct wlr_output *b, int wb, struct sh_exchange *exchange) {
    struct sh_rect area_a = usable_area(server, a), area_b = usable_area(server, b);
    struct wlr_box box_a = {area_a.x, area_a.y, area_a.width, area_a.height};
    struct wlr_box box_b = {area_b.x, area_b.y, area_b.width, area_b.height};
    size_t first = exchange->count;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!travels_with_workspace(toplevel))
            continue;
        bool from_a = !strcmp(toplevel->output, a->name) && toplevel->workspace == wa;
        bool from_b = !strcmp(toplevel->output, b->name) && toplevel->workspace == wb;
        if (!from_a && !from_b)
            continue;
        exchange_note(exchange, toplevel);
    }
    size_t last = exchange->count;
    sh_tiling_exchange(server->tiling, a->name, wa, b->name, wb);
    for (size_t i = first; i < last; ++i) {
        toplevel = exchange->windows[i];
        bool from_a = !strcmp(toplevel->output, a->name);
        struct wlr_output *to = from_a ? b : a;
        struct wlr_box from_box = from_a ? box_a : box_b;
        struct sh_rect area = from_a ? area_b : area_a;
        toplevel->restore_box = translate_box(toplevel->restore_box, from_box, area);
        toplevel->fullscreen_restore = translate_box(toplevel->fullscreen_restore, from_box, area);
        struct wlr_box moved = translate_box(toplevel_box(toplevel), from_box, area);
        snprintf(toplevel->output, sizeof(toplevel->output), "%s", to->name);
        toplevel->workspace = from_a ? wb : wa;
        toplevel->home_output[0] = '\0';
        if (toplevel->group_hidden || toplevel->swallowed || !toplevel_mapped(toplevel))
            continue;
        if (toplevel->tiled) {
            if (!output_tiles(server, to)) {
                // A window that opened tiled floats where its tile is, which must be on `to`.
                wlr_scene_node_set_position(&toplevel->scene_tree->node, moved.x, moved.y);
                untile_toplevel(toplevel, true);
            }
            continue;
        }
        place_on_output(toplevel, to, moved);
        if (wants_tiling(toplevel, to))
            tile_toplevel_at(toplevel, to, NULL, false, 0, 0);
    }
}

/* The last steps of an exchange: what is on screen, the arrangement of both outputs, and a glide
 * from where each window was drawn. */
static void finish_exchange(struct sh_server *server, struct wlr_output *a, struct wlr_output *b,
                            struct sh_exchange *exchange) {
    show_workspaces(server);
    // Floating windows glide from where they were; tiles glide in reflow_output.
    for (size_t i = 0; i < exchange->count; ++i) {
        struct sh_toplevel *toplevel = exchange->windows[i];
        if (toplevel->tiled || !toplevel->shown || !toplevel_visible(toplevel))
            continue;
        struct wlr_scene_node *node = &toplevel->scene_tree->node;
        sh_anim_glide_kind(server->animator, &toplevel->anim, toplevel->content,
                           exchange->x[i] - node->x, exchange->y[i] - node->y, SH_ANIM_MOVE);
    }
    reflow_output(server, a);
    reflow_output(server, b);
    refit_fullscreen(server);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    if (server->focused_toplevel) {
        if (!toplevel_visible(server->focused_toplevel)) {
            deactivate_toplevel(server);
            focus_previous(server);
        } else {
            set_active_output(server, server->focused_toplevel->output);
        }
    }
    notify_subscribers(server);
}

/* move_workspace_to_output: the focused output's workspace goes to another output, which shows
 * it; the workspace with the same number there takes its place. */
static void move_workspace_to_output(struct sh_server *server, const char *target) {
    struct wlr_output *from = focused_output(server);
    struct wlr_output *to = from ? resolve_output_target(server, from, target) : NULL;
    if (!to) {
        wlr_log(WLR_INFO, "move_workspace_to_output: no output %s from %s", target,
                from ? from->name : "here");
        return;
    }
    int workspace = *output_workspace(server, from->name);
    struct sh_exchange exchange = {0};
    ++server->reflow_held;
    exchange_workspace_slots(server, from, workspace, to, workspace, &exchange);
    --server->reflow_held;
    show_workspace(server, to->name, workspace);
    finish_exchange(server, from, to, &exchange);
    wlr_log(WLR_INFO, "Workspace %d moved from %s to %s", workspace + 1, from->name, to->name);
    free(exchange.windows);
    free(exchange.x);
    free(exchange.y);
}

/* swap_workspaces: the focused output and another trade all their workspaces, and with them
 * what they show. */
static void swap_output_workspaces(struct sh_server *server, const char *target) {
    struct wlr_output *first = focused_output(server);
    struct wlr_output *second = first ? resolve_output_target(server, first, target) : NULL;
    if (!second) {
        wlr_log(WLR_INFO, "swap_workspaces: no output %s from %s", target,
                first ? first->name : "here");
        return;
    }
    struct sh_exchange exchange = {0};
    ++server->reflow_held;
    for (int workspace = 0; workspace < server_settings(server)->workspaces; ++workspace)
        exchange_workspace_slots(server, first, workspace, second, workspace, &exchange);
    --server->reflow_held;
    int a = output_slot(server, first->name), b = output_slot(server, second->name);
    int current = server->output_workspaces[a].current, previous = server->output_workspaces[a].previous;
    server->output_workspaces[a].current = server->output_workspaces[b].current;
    server->output_workspaces[a].previous = server->output_workspaces[b].previous;
    server->output_workspaces[b].current = current;
    server->output_workspaces[b].previous = previous;
    finish_exchange(server, first, second, &exchange);
    wlr_log(WLR_INFO, "Workspaces of %s and %s swapped", first->name, second->name);
    free(exchange.windows);
    free(exchange.x);
    free(exchange.y);
}

/* Tiles of an output disabled in the config join the tiling of the output they are nearest
 * now, or float there if it does not tile; windows that floated only because their output did
 * not tile join the tiling of one that does. Tiles of an unplugged output wait for it:
 * monitors drop off when they sleep. */
static void rehome_tiles(struct sh_server *server) {
    if (!first_output(server))
        return;
    bool moved = false;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (toplevel->tiled && !tiled_output(toplevel)) {
            untile_toplevel(toplevel, false);
            if (wants_tiling(toplevel, NULL))
                tile_toplevel(toplevel, NULL, NULL, false);
            else
                restore_toplevel(toplevel);
            moved = true;
        } else if (toplevel != server->grabbed_toplevel && toplevel_mapped(toplevel) &&
                   wants_tiling(toplevel, NULL)) {
            tile_toplevel(toplevel, NULL, NULL, false);
            moved = true;
        }
    }
    if (moved)
        refit_fullscreen(server); // Fullscreen tiles follow to their new output.
}

/* Turns automatic tiling of `output` on or off, for the windows on all its workspaces. */
static void set_tiling(struct sh_server *server, struct wlr_output *output, bool enabled) {
    if (!output || output_tiles(server, output) == enabled)
        return;
    server->output_workspaces[output_slot(server, output->name)].tiling = enabled;
    // Most recently focused first, so the focused window gets the largest tile. Tiles of an
    // unplugged output are left waiting for it.
    // Placing every window after each one joins or leaves would take time quadratic in the
    // number of windows; the reflow after the loop places them once.
    ++server->reflow_held;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->tiled ? tiled_output(toplevel) != output : home_output(toplevel) != output)
            continue;
        if (enabled && wants_tiling(toplevel, output))
            tile_toplevel(toplevel, output, NULL, false);
        else if (!enabled && toplevel->tiled)
            untile_toplevel(toplevel, true);
        else if (!enabled && toplevel->arranged && toplevel->arrangement == SH_NONE)
            restore_toplevel(toplevel); // left the tiling while minimized
    }
    --server->reflow_held;
    reflow_output(server, output); // maximized windows gain or lose their border
    // Floating windows, which no reflow touches, gain or lose their rounded corners.
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    wlr_log(WLR_INFO, "Tiling %s on %s", enabled ? "on" : "off", output->name);
    notify_subscribers(server);
}

/* The rectangle a tile got in the last arrangement. */
struct tile_lookup {
    void *window;
    struct sh_rect rect;
    bool found;
};
static void find_tile(void *data, void *window, struct sh_rect rect) {
    struct tile_lookup *lookup = data;
    if (window == lookup->window) {
        lookup->rect = rect;
        lookup->found = true;
    }
}

/* Moves a tile to the far side of `neighbour`, as Hyprland's dwindle layout does: out of the
 * tiling, then in again splitting the neighbour on the side away from where it came from. */
static void move_tile(struct sh_toplevel *toplevel, struct sh_toplevel *neighbour,
                      struct wlr_output *output, bool horizontal, int sign) {
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), SH_TILE);
    struct tile_lookup lookup = {neighbour, {0}, false};
    sh_tiling_remove(server->tiling, toplevel);
    // Arranging without the window gives the neighbour the box it is split from.
    sh_tiling_arrange(server->tiling, output->name, toplevel->workspace, area, settings->gap_inner,
                      find_tile, &lookup);
    struct sh_rect r = lookup.rect;
    double x = r.x + r.width / 2.0, y = r.y + r.height / 2.0;
    if (horizontal)
        x = sign < 0 ? r.x : r.x + r.width - 1;
    else
        y = sign < 0 ? r.y : r.y + r.height - 1;
    sh_tiling_insert(server->tiling, output->name, toplevel->workspace, toplevel,
                     lookup.found ? neighbour : NULL, lookup.found, x, y);
    reflow_output(server, output);
}

/* The usable area of `output` less the outer gap and the window border: where a floating
 * window moved by the keyboard may go. */
static struct sh_rect floating_area(struct sh_server *server, struct wlr_output *output) {
    int gap = server_settings(server)->gap_outer;
    struct sh_rect area = usable_area(server, output);
    area = (struct sh_rect){area.x + gap, area.y + gap, area.width - 2 * gap,
                            area.height - 2 * gap};
    return inside_border(server, area);
}

/* `box` placed against the side of `area` facing the direction, kept inside `area` across
 * it. */
static struct wlr_box against_edge(struct wlr_box box, struct sh_rect area, bool horizontal,
                                   int sign) {
    int x = sign < 0 ? area.x : fmax(area.x, area.x + area.width - box.width);
    int y = sign < 0 ? area.y : fmax(area.y, area.y + area.height - box.height);
    if (horizontal) {
        box.x = x;
        box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    } else {
        box.y = y;
        box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    }
    return box;
}

/* A snapped or grid-arranged window leaves its arrangement to be moved: it gets its floating
 * size back where it is. */
static void unarrange_in_place(struct sh_toplevel *toplevel) {
    if (!toplevel->arranged)
        return;
    struct wlr_box box = toplevel_box(toplevel);
    if (toplevel->restore_box.width > 0 && toplevel->restore_box.height > 0) {
        box.width = toplevel->restore_box.width;
        box.height = toplevel->restore_box.height;
    }
    toplevel->arranged = false;
    toplevel_set_states(toplevel, false, 0);
    toplevel_configure_box(toplevel, box);
}

/* Moves the window over to `next`, onto the side facing where it came from: into its tiling
 * when the window tiles there, else floating against that edge. A maximized window stays
 * maximized. */
static void move_to_output(struct sh_toplevel *toplevel, struct wlr_output *next, bool horizontal,
                           int sign) {
    struct sh_server *server = toplevel->server;
    struct sh_rect area = usable_area(server, next);
    struct wlr_box box = toplevel_box(toplevel);
    untile_toplevel(toplevel, false);
    if (wants_tiling(toplevel, next)) {
        double x = fmax(area.x, fmin(box.x + box.width / 2.0, area.x + area.width - 1));
        double y = fmax(area.y, fmin(box.y + box.height / 2.0, area.y + area.height - 1));
        if (horizontal)
            x = sign < 0 ? area.x + area.width - 1 : area.x;
        else
            y = sign < 0 ? area.y + area.height - 1 : area.y;
        tile_toplevel_at(toplevel, next, NULL, true, x, y);
        return;
    }
    if (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE) {
        place_toplevel(toplevel, SH_MAXIMIZE, area);
        return;
    }
    unarrange_in_place(toplevel);
    box = rebase_box(server, toplevel_box(toplevel), next);
    toplevel_configure_box(toplevel, against_edge(box, floating_area(server, next), horizontal,
                                                  -sign));
}

/* Hyprland's movewindow. A tile trades places with the nearest tile that way; a floating
 * window moves to that edge of its output. From the edge, either moves on to the next output
 * that way, if there is one. Neither ever covers another tile or grows to fill half the
 * output. */
static void move_window(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!toplevel || server->locked || toplevel->fullscreen)
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    bool horizontal = action == SH_MOVE_LEFT || action == SH_MOVE_RIGHT;
    int sign = action == SH_MOVE_LEFT || action == SH_MOVE_UP ? -1 : 1;
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    if (toplevel->tiled) {
        bool strip = toplevel_layout(toplevel) == SH_LAYOUT_SCROLL;
        if (strip && horizontal) {
            // Moves the whole column along the strip; from its end, on to the next output.
            if (sh_tiling_scroll_move(server->tiling, toplevel, sign)) {
                sh_tiling_set_focus(server->tiling, toplevel);
                reflow_output(server, output);
                pointer_follow(toplevel);
                return;
            }
        }
        bool monocle = toplevel_layout(toplevel) == SH_LAYOUT_MONOCLE;
        struct sh_toplevel *neighbour =
            strip ? (horizontal ? NULL
                                : sh_tiling_scroll_step(server->tiling, toplevel, 0, sign))
                  : monocle ? sh_tiling_neighbour(server->tiling, toplevel, sign)
                            : toplevel_toward(toplevel, horizontal, sign, true);
        if (neighbour && toplevel_layout(toplevel) != SH_LAYOUT_DWINDLE) {
            // Outside dwindle, tiles keep their places in the list: trade with the neighbour.
            sh_tiling_swap(server->tiling, toplevel, neighbour);
            reflow_output(server, output);
            pointer_follow(toplevel);
            return;
        }
        if (neighbour) {
            move_tile(toplevel, neighbour, output, horizontal, sign);
            pointer_follow(toplevel);
            return;
        }
    } else if (!toplevel->arranged || toplevel->arrangement != SH_MAXIMIZE) {
        unarrange_in_place(toplevel);
        struct wlr_box box = toplevel_box(toplevel);
        struct wlr_box moved =
            against_edge(box, floating_area(server, output), horizontal, sign);
        if (moved.x != box.x || moved.y != box.y) {
            toplevel_set_position(toplevel, moved.x, moved.y);
            pointer_follow(toplevel);
            return;
        }
    }
    static const enum wlr_direction directions[] = {WLR_DIRECTION_LEFT, WLR_DIRECTION_RIGHT,
                                                    WLR_DIRECTION_UP, WLR_DIRECTION_DOWN};
    struct wlr_box box = toplevel_box(toplevel);
    struct wlr_output *next = wlr_output_layout_adjacent_output(
        server->output_layout, directions[action - SH_MOVE_LEFT], output,
        box.x + box.width / 2.0, box.y + box.height / 2.0);
    if (!next)
        return;
    move_to_output(toplevel, next, horizontal, sign);
    pointer_follow(toplevel);
}

/* The smallest size keyboard resizing shrinks a floating window to, unless the client asks
 * for more. */
#define SH_RESIZE_MIN 64

/* Keyboard resizing by `amount` pixels. A tile moves the split on that side of it that way,
 * growing it, or, touching the output on that side, the split on its other side, shrinking
 * it. A floating window moves its right or bottom edge that way, growing no further than its
 * output's edge and shrinking no smaller than SH_RESIZE_MIN. A snapped window leaves its
 * arrangement at its current size; maximized and fullscreen windows stay as they are, and
 * features.keyboard_resize = false turns this off. */
static void resize_window(struct sh_server *server, enum sh_action action, int amount) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!server_settings(server)->keyboard_resize || !toplevel || server->locked ||
        toplevel->fullscreen || amount <= 0 ||
        (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE))
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    static const uint32_t directions[] = {SH_EDGE_LEFT, SH_EDGE_RIGHT, SH_EDGE_TOP,
                                          SH_EDGE_BOTTOM};
    uint32_t direction = directions[action - SH_RESIZE_LEFT];
    if (toplevel->tiled) {
        struct wlr_output *output = tiled_output(toplevel);
        if (output && sh_tiling_resize_by(server->tiling, toplevel, direction, amount))
            reflow_output(server, output);
        return;
    }
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    struct sh_rect area = floating_area(server, output);
    struct wlr_box box = toplevel_box(toplevel);
    bool horizontal = direction == SH_EDGE_LEFT || direction == SH_EDGE_RIGHT;
    int shift = direction == SH_EDGE_RIGHT || direction == SH_EDGE_BOTTOM ? amount : -amount;
    int start = horizontal ? box.x : box.y, size = horizontal ? box.width : box.height;
    int limit = horizontal ? area.x + area.width : area.y + area.height;
    int least = SH_RESIZE_MIN;
    if (toplevel->xdg_toplevel) {
        const struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
        least = fmax(least, horizontal ? state->min_width : state->min_height);
    }
    // A window already past the edge, or already below the minimum, gets no worse.
    int end = fmin(start + size + shift, fmax(limit, start + size));
    int resized = fmax(end - start, fmin(least, size));
    if (resized == size)
        return;
    if (toplevel->arranged) {
        toplevel->arranged = false;
        toplevel_set_states(toplevel, false, 0);
    }
    if (horizontal)
        box.width = resized;
    else
        box.height = resized;
    toplevel_configure_box(toplevel, box);
}

/* A reload applies tiling settings that changed in the config; outputs toggled since keep
 * their state while the config for them stays the same. */
static void configure_layouts(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    sh_tiling_set_defaults(server->tiling, (enum sh_tile_layout)settings->tile_layout,
                           settings->master_ratio, settings->master_count);
    sh_tiling_set_scroll(server->tiling, (enum sh_scroll_follow)settings->scroll_follow,
                         settings->scroll_width, settings->scroll_step, settings->scroll_presets,
                         settings->scroll_preset_count);
}

/* layout.outputs for one output: the entry by connector name wins over one by description. */
static void apply_output_layout(struct sh_server *server, const struct wlr_output *output) {
    const struct sh_settings *settings = server_settings(server);
    const struct sh_output_layout *found = NULL;
    for (int i = 0; i < settings->output_layout_count; ++i) {
        const struct sh_output_layout *entry = &settings->output_layouts[i];
        if (!output_key_matches(entry->name, output))
            continue;
        if (strncmp(entry->name, "desc:", 5) != 0) {
            found = entry;
            break;
        }
        found = found ? found : entry;
    }
    if (found)
        sh_tiling_set_output_defaults(server->tiling, output->name, found->tile_layout,
                                      found->master_ratio, found->master_count);
    else
        sh_tiling_set_output_defaults(server->tiling, output->name, -1, 0, 0);
}

/* Gives every connected output its layout.outputs defaults; the caller reflows. */
static void apply_output_layouts(struct sh_server *server) {
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) apply_output_layout(server, output->wlr_output);
    }
}

static void reconfigure_tiling(struct sh_server *server) {
    configure_layouts(server);
    sh_tiling_clear_output_defaults(server->tiling);
    apply_output_layouts(server);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        int slot = output_slot(server, output->wlr_output->name);
        if (server->output_workspaces[slot].tiling < 0)
            continue;
        bool configured = configured_tiling(server, output->wlr_output);
        if (configured == server->output_workspaces[slot].configured)
            continue;
        server->output_workspaces[slot].configured = configured;
        set_tiling(server, output->wlr_output, configured);
    }
}

static void foreign_activate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_activate);
    struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    if (event->seat == toplevel->server->seat)
        focus_toplevel(toplevel);
}
static void foreign_close(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_close);
    toplevel_close(toplevel);
}
static void foreign_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    if (toplevel->fullscreen)
        return; // As for the client's own request: fullscreen wins.
    if (!event->maximized) {
        restore_toplevel(toplevel);
        return;
    }
    if (toplevel->tiled) {
        toplevel->floating = toplevel->placed = true;
        untile_toplevel(toplevel, false);
    }
    place_maximized(toplevel);
}
static void foreign_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    set_fullscreen(toplevel, event->fullscreen);
}
static void foreign_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    if (event->minimized)
        minimize_toplevel(toplevel);
    else
        focus_toplevel(toplevel);
}
static void toplevel_request_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
    if (toplevel_mapped(toplevel))
        minimize_toplevel(toplevel);
}
static void update_listed_state(struct sh_toplevel *toplevel) {
    if (!toplevel->listed)
        return;
    const char *title = toplevel_title(toplevel), *app_id = toplevel_app_id(toplevel);
    struct wlr_ext_foreign_toplevel_handle_v1_state state = {title ? title : "Untitled",
                                                             app_id ? app_id : ""};
    wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->listed, &state);
}
static void toplevel_title_changed(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, title_changed);
    const char *title = toplevel_title(toplevel);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign, title ? title : "Untitled");
    update_listed_state(toplevel);
    refresh_frame(toplevel); // opacity rules may match the title
    if (toplevel->urgent)
        notify_subscribers(toplevel->server); // the shell finds the window by its title
}
static void toplevel_app_id_changed(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, app_id_changed);
    const char *app_id = toplevel_app_id(toplevel);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign, app_id ? app_id : "");
    update_listed_state(toplevel);
    if (toplevel->urgent)
        notify_subscribers(toplevel->server);
}
static void list_toplevel(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    toplevel->capture_scene = wlr_scene_create();
    if (!toplevel->capture_scene)
        return;
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        wlr_scene_subsurface_tree_create(&toplevel->capture_scene->tree,
                                         toplevel_surface(toplevel));
    else
#endif
        wlr_scene_xdg_surface_create(&toplevel->capture_scene->tree, toplevel->xdg_toplevel->base);
    struct wlr_ext_foreign_toplevel_handle_v1_state state = {"", ""};
    toplevel->listed = wlr_ext_foreign_toplevel_handle_v1_create(server->toplevel_list, &state);
    if (toplevel->listed) {
        toplevel->listed->data = toplevel;
        update_listed_state(toplevel);
    }
}
static void unlist_toplevel(struct sh_toplevel *toplevel) {
    if (toplevel->listed)
        wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->listed);
    toplevel->listed = NULL;
    // Destroying the scene also ends any capture source made from it.
    if (toplevel->capture_scene)
        wlr_scene_node_destroy(&toplevel->capture_scene->tree.node);
    toplevel->capture_scene = NULL;
    toplevel->capture_source = NULL;
}
static void server_new_capture_request(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_capture_request);
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
    struct sh_toplevel *toplevel = request->toplevel_handle->data;
    if (!toplevel || !toplevel->capture_scene || server->locked)
        return;
    if (!toplevel->capture_source)
        toplevel->capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
            &toplevel->capture_scene->tree.node, wl_display_get_event_loop(server->wl_display),
            server->allocator, server->renderer);
    if (toplevel->capture_source)
        wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
            request, toplevel->capture_source);
}
static void publish_toplevel(struct sh_toplevel *toplevel) {
    list_toplevel(toplevel);
    toplevel->foreign = wlr_foreign_toplevel_handle_v1_create(toplevel->server->foreign_manager);
    if (!toplevel->foreign)
        return;
    toplevel_title_changed(&toplevel->title_changed, NULL);
    toplevel_app_id_changed(&toplevel->app_id_changed, NULL);
    add_listener(&toplevel->foreign->events.request_activate, &toplevel->foreign_activate,
                 foreign_activate);
    add_listener(&toplevel->foreign->events.request_close, &toplevel->foreign_close, foreign_close);
    add_listener(&toplevel->foreign->events.request_maximize, &toplevel->foreign_maximize,
                 foreign_maximize);
    add_listener(&toplevel->foreign->events.request_minimize, &toplevel->foreign_minimize,
                 foreign_minimize);
    add_listener(&toplevel->foreign->events.request_fullscreen, &toplevel->foreign_fullscreen,
                 foreign_fullscreen);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign, toplevel->fullscreen);
    struct wlr_output *output = toplevel_output(toplevel);
    if (output)
        wlr_foreign_toplevel_handle_v1_output_enter(toplevel->foreign, output);
}
static void unpublish_toplevel(struct sh_toplevel *toplevel) {
    unlist_toplevel(toplevel);
    if (!toplevel->foreign)
        return;
    wl_list_remove(&toplevel->foreign_activate.link);
    wl_list_remove(&toplevel->foreign_close.link);
    wl_list_remove(&toplevel->foreign_maximize.link);
    wl_list_remove(&toplevel->foreign_minimize.link);
    wl_list_remove(&toplevel->foreign_fullscreen.link);
    wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign);
    toplevel->foreign = NULL;
}

/* Reserve exclusive panel regions before positioning nonexclusive layers. */
static void arrange_layers(struct sh_server *server) {
    struct sh_output *output;
    struct sh_layer *exclusive = NULL;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box full, usable;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
        usable = full;
        for (int pass = 0; pass < 2; ++pass) {
            for (int level = 3; level >= 0; --level) {
                struct sh_layer *layer;
                wl_list_for_each(layer, &server->layers, link) {
                    struct wlr_layer_surface_v1 *surface = layer->surface;
                    if (surface->output != output->wlr_output || !surface->initialized ||
                        (!surface->surface->mapped && !surface->initial_commit) ||
                        surface->current.layer != (unsigned)level ||
                        (surface->current.exclusive_zone > 0) != (pass == 0))
                        continue;
                    wlr_scene_node_reparent(&layer->scene->tree->node, server->layer_trees[level]);
                    wlr_scene_layer_surface_v1_configure(layer->scene, &full, &usable);
                    if (!exclusive && surface->surface->mapped && level >= 2 &&
                        surface->current.keyboard_interactive ==
                            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
                        exclusive = layer;
                }
            }
        }
        if (!wlr_box_equal(&output->usable, &usable)) {
            output->usable = usable;
            reflow_output(server, output->wlr_output);
        }
    }
    if (exclusive)
        focus_layer(exclusive);
    else if (server->focused_layer &&
             (!server->focused_layer->surface->surface->mapped ||
              server->focused_layer->surface->current.keyboard_interactive ==
                  ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE))
        focus_previous(server);
}
static void layer_commit(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, commit);
    if (layer->surface->initial_commit || layer->surface->current.committed)
        arrange_layers(layer->server);
}
static void layer_map(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, map);
    arrange_layers(layer->server);
}
static void layer_unmap(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, unmap);
    if (layer->server->focused_layer == layer)
        focus_previous(layer->server);
    arrange_layers(layer->server);
}
static void layer_destroy(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, destroy);
    struct sh_server *server = layer->server;
    if (server->focused_layer == layer)
        focus_previous(server);
    wl_list_remove(&layer->commit.link);
    wl_list_remove(&layer->map.link);
    wl_list_remove(&layer->unmap.link);
    wl_list_remove(&layer->destroy.link);
    wl_list_remove(&layer->new_popup.link);
    wl_list_remove(&layer->link);
    free(layer);
    arrange_layers(server);
}
static void layer_new_popup(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, new_popup);
    create_popup(layer->server, data, layer->scene->tree);
}
static void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *surface = data;
    if (!surface->output && !(surface->output = first_output(server))) {
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    struct sh_layer *layer = calloc(1, sizeof(*layer));
    if (!layer) {
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    layer->server = server;
    layer->surface = surface;
    layer->node = (struct sh_node){SH_NODE_LAYER, layer};
    layer->scene =
        wlr_scene_layer_surface_v1_create(server->layer_trees[surface->pending.layer], surface);
    if (!layer->scene) {
        free(layer);
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    layer->scene->tree->node.data = &layer->node;
    surface->data = layer;
    wl_list_insert(&server->layers, &layer->link);
    add_listener(&surface->surface->events.commit, &layer->commit, layer_commit);
    add_listener(&surface->surface->events.map, &layer->map, layer_map);
    add_listener(&surface->surface->events.unmap, &layer->unmap, layer_unmap);
    add_listener(&surface->events.destroy, &layer->destroy, layer_destroy);
    add_listener(&surface->events.new_popup, &layer->new_popup, layer_new_popup);
}

static void maximize_toplevel(struct sh_toplevel *toplevel, bool maximized) {
    if (toplevel->tiled) {
        toplevel_refresh(toplevel); // Tiles ignore client maximize requests, as in Hyprland.
        return;
    }
    if (maximized)
        place_maximized(toplevel);
    else
        restore_toplevel(toplevel);
}

/* The actions windows.rules give a window as it opens; false when none apply. */
static bool window_rule(struct sh_toplevel *toplevel, struct sh_window_rule *rule) {
    const struct sh_callbacks *callbacks = toplevel->server->callbacks;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    *rule = (struct sh_window_rule){.floating = -1};
    return callbacks->window_rule(callbacks->userdata, app_id ? app_id : "", title ? title : "",
                                  rule);
}

/* The enabled output a window rule names by connector, or by "desc:" and the start of its
 * "make model serial". */
static struct wlr_output *rule_output(struct sh_server *server, const char *name) {
    bool described = strncmp(name, "desc:", 5) == 0;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->disabled)
            continue;
        char description[256];
        output_description(output->wlr_output, description, sizeof(description));
        if (described ? strncmp(description, name + 5, strlen(name + 5)) == 0
                      : output_named(output, name))
            return output->wlr_output;
    }
    return NULL;
}

/* New windows open on the output under the pointer, as in Hyprland. */
static struct wlr_output *new_window_output(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    return output ? output : toplevel_output(toplevel);
}

/* Where a new window on `output` joins the tiling, as in Hyprland: it splits the focused tile
 * when that is on the pointer's output, else the tile under the pointer. Returns the output of
 * the tree it joins and sets `target` to the tile it splits, if any. */
static struct wlr_output *new_tile_split(struct sh_toplevel *toplevel, struct wlr_output *output,
                                         struct sh_toplevel **target) {
    struct sh_toplevel *previous = toplevel->server->focused_toplevel;
    bool split_focused = previous && previous != toplevel && previous->tiled &&
                         toplevel_visible(previous) &&
                         (!output || tiled_output(previous) == output);
    *target = split_focused ? previous : NULL;
    return split_focused ? tiled_output(previous) : output;
}

/* The tile a new xdg-shell window will get when it maps, sent with its first configure so the
 * first buffer already fits: otherwise it is drawn at its own size, shown there, and drawn
 * again at the tile's size. */
static bool initial_tile_size(struct sh_toplevel *toplevel, int *width, int *height) {
    struct sh_server *server = toplevel->server;
    if (toplevel->xdg_toplevel->requested.fullscreen || toplevel_is_dialog(toplevel))
        return false;
    // A rule that places the window elsewhere or keeps it out of the tiling decides at map.
    struct sh_window_rule rule;
    if (window_rule(toplevel, &rule) && (rule.floating == 1 || rule.workspace || rule.output[0] ||
                                         rule.fullscreen || rule.maximize || rule.sticky))
        return false;
    struct sh_toplevel *target;
    struct wlr_output *output = new_tile_split(toplevel, new_window_output(toplevel), &target);
    if (!output_tiles(server, output))
        return false;
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), SH_TILE), rect;
    if (!sh_tiling_preview(server->tiling, output->name, *output_workspace(server, output->name),
                           toplevel, target, true, server->cursor->x, server->cursor->y, area,
                           settings->gap_inner, &rect))
        return false;
    rect = inside_border(server, rect);
    *width = rect.width;
    *height = rect.height;
    return true;
}

/* Takes the windows on the current workspace of `output`, other than `toplevel`, out of
 * fullscreen. */
static void leave_fullscreen_for(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_toplevel *other, *tmp;
    wl_list_for_each_safe(other, tmp, &toplevel->server->toplevels, link) {
        if (other != toplevel && other->fullscreen && toplevel_visible(other) &&
            toplevel_output(other) == output)
            set_fullscreen(other, false);
    }
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* A window a session restore launched takes over the saved window's place as its rule: the
 * output, workspace, floating place and state it had. Returns whether one matched. */
static bool session_claim(struct sh_server *server, struct sh_toplevel *toplevel,
                          struct sh_window_rule *rule, bool ruled) {
    const char *app_id = toplevel_app_id(toplevel);
    int64_t now = monotonic_ms();
    for (size_t i = 0; i < sizeof(server->session_pending) / sizeof(*server->session_pending);
         ++i) {
        if (server->session_pending[i].used && server->session_pending[i].deadline < now)
            server->session_pending[i].used = false;
    }
    for (size_t i = 0; i < sizeof(server->session_pending) / sizeof(*server->session_pending);
         ++i) {
        if (!server->session_pending[i].used ||
            strcmp(server->session_pending[i].window.app_id, app_id ? app_id : ""))
            continue;
        const struct sh_session_window *saved = &server->session_pending[i].window;
        server->session_pending[i].used = false;
        if (!ruled)
            memset(rule, 0, sizeof(*rule)), rule->floating = -1;
        snprintf(rule->output, sizeof(rule->output), "%s", saved->output);
        rule->workspace = saved->workspace + 1;
        rule->floating = saved->flags & SH_SESSION_TILED ? 0 : saved->flags & SH_SESSION_FLOATING ? 1 : -1;
        rule->fullscreen = saved->flags & SH_SESSION_FULLSCREEN;
        rule->maximize = saved->flags & SH_SESSION_MAXIMIZED;
        rule->sticky = saved->flags & SH_SESSION_STICKY;
        rule->no_focus = !(saved->flags & SH_SESSION_FOCUSED);
        rule->width = rule->height = 0;
        rule->position = SH_RULE_POSITION_UNSET;
        struct wlr_output *output = find_output(server, saved->output);
        if (output && saved->width > 0 && saved->height > 0) {
            struct sh_rect area = usable_area(server, output);
            rule->width = saved->width;
            rule->height = saved->height;
            rule->position = SH_RULE_POSITION_AT;
            rule->x = saved->x - area.x;
            rule->y = saved->y - area.y;
        }
        return true;
    }
    return ruled;
}

#define SH_PLACE_OTHERS 32

static void map_toplevel(struct sh_toplevel *toplevel, bool fullscreen, bool maximized) {
    struct sh_server *server = toplevel->server;
    int offset = 40 + 32 * (wl_list_length(&toplevel->server->toplevels) % 8);
    int x = offset, y = offset;
    bool resize = false;
    struct wlr_box geometry = toplevel_geometry(toplevel);
    int width = geometry.width, height = geometry.height;
    struct sh_window_rule rule;
    bool ruled = window_rule(toplevel, &rule);
    ruled = session_claim(server, toplevel, &rule, ruled);
    struct wlr_output *output = new_window_output(toplevel);
    if (ruled && rule.output[0]) {
        struct wlr_output *named = rule_output(server, rule.output);
        output = named ? named : output;
    }
    // It opens on that output's current workspace, even if it was mapped there before, unless
    // a rule names another.
    toplevel->output[0] = '\0';
    toplevel->workspace = 0;
    toplevel->sticky = false;
    set_toplevel_output(toplevel, output);
    if (ruled && output && rule.workspace > 0 &&
        rule.workspace <= server_settings(server)->workspaces)
        toplevel->workspace = rule.workspace - 1;
    if (output) {
        struct sh_rect area = usable_area(server, output);
        // Keep newly opened applications reachable inside a small nested output.
        int margin = area.width < 80 || area.height < 80 ? 0 : 40;
        if (width > area.width - 2 * margin) {
            width = area.width - 2 * margin;
            resize = true;
        }
        if (height > area.height - 2 * margin) {
            height = area.height - 2 * margin;
            resize = true;
        }
        x = area.x + (offset + width <= area.width ? offset : margin);
        y = area.y + (offset + height <= area.height ? offset : margin);
        // Rules size and place it within the usable area; a tile keeps this as its floating
        // geometry.
        if (ruled && rule.width > 0 && rule.height > 0) {
            width = rule.width < area.width ? rule.width : area.width;
            height = rule.height < area.height ? rule.height : area.height;
            resize = true;
        }
        // Where the windows already there leave room, unless a rule names the place.
        if (!(ruled && rule.position != SH_RULE_POSITION_UNSET) && width > 0 && height > 0) {
            struct sh_rect others[SH_PLACE_OTHERS], place;
            int count = 0, cascade = 0;
            struct sh_toplevel *other;
            wl_list_for_each(other, &server->toplevels, link) {
                ++cascade;
                if (count < SH_PLACE_OTHERS && toplevel_mapped(other) && toplevel_visible(other) &&
                    !other->fullscreen && toplevel_output(other) == output &&
                    other->workspace == toplevel->workspace) {
                    struct wlr_box box = toplevel_box(other);
                    others[count++] = (struct sh_rect){box.x, box.y, box.width, box.height};
                }
            }
            if (sh_place_window(server_settings(server)->placement, (struct sh_rect){area.x, area.y,
                                area.width, area.height}, others, count, width, height, cascade,
                                &place)) {
                x = place.x;
                y = place.y;
            }
        }
        if (ruled && rule.position == SH_RULE_POSITION_CENTER) {
            x = area.x + (area.width - width) / 2;
            y = area.y + (area.height - height) / 2;
        } else if (ruled && rule.position == SH_RULE_POSITION_AT) {
            x = area.x + rule.x;
            y = area.y + rule.y;
        }
    }
    if (resize && width > 0 && height > 0)
        toplevel_configure(toplevel, x, y, width, height);
    else
        toplevel_set_position(toplevel, x, y);
    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    publish_toplevel(toplevel);
    toplevel->floating = toplevel_is_dialog(toplevel);
    if (ruled && rule.floating >= 0)
        toplevel->floating = rule.floating;
    // Sticky floats it on the current workspace of its output, whatever `workspace` says.
    if (ruled && rule.sticky && server_settings(server)->sticky)
        set_sticky(toplevel, true, false);
    // A window started from a terminal takes its place, unless a rule puts it elsewhere.
    struct sh_toplevel *terminal = NULL;
    if (swallow_wanted(toplevel) &&
        !(ruled && (rule.floating == 1 || rule.workspace || rule.output[0] || rule.sticky))) {
        terminal = swallow_host(toplevel, true);
        if (terminal) {
            set_toplevel_output(toplevel, find_output(server, terminal->output));
            toplevel->workspace = terminal->workspace;
        }
    }
    // A window a rule sends to another workspace, or opens without focus, stays out of the way.
    bool visible = toplevel_visible(toplevel);
    bool focus = visible && !(ruled && rule.no_focus);
    fullscreen = fullscreen || (ruled && rule.fullscreen);
    // A new window would open over a fullscreen one on its workspace, so that one leaves
    // fullscreen first, as in Hyprland; dialogs belong to it and may show over it.
    if (output && !fullscreen && !toplevel->floating && focus)
        leave_fullscreen_for(toplevel, output);
    struct sh_toplevel *target = NULL;
    struct wlr_output *tile_output = visible ? new_tile_split(toplevel, output, &target) : output;
    // Opening while a group has focus adds a tab to it, taking the group's slot.
    struct sh_toplevel *host = server->focused_toplevel;
    bool joins = visible && !terminal && host && host != toplevel && host->group && groupable(host) &&
                 groups_enabled(server) && server_settings(server)->group_join_new &&
                 !fullscreen && !toplevel_is_dialog(toplevel) && !toplevel->sticky &&
                 !(ruled && (rule.floating == 1 || rule.workspace || rule.output[0])) &&
                 host->workspace == toplevel->workspace && !strcmp(host->output, toplevel->output);
    if (terminal) {
        swallow_attach(terminal, toplevel);
    } else if (joins) {
        toplevel->group_hidden = true;
        group_join(toplevel, host->group);
        toplevel->floating = host->floating;
        group_show(toplevel);
    } else if (wants_tiling(toplevel, tile_output)) {
        tile_toplevel(toplevel, tile_output, target, visible);
    } else if (toplevel->tile_sized) {
        toplevel->tile_sized = false; // It floats after all: let the client choose its size.
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure(toplevel, x, y, resize ? width : 0, resize ? height : 0);
    }
    if (focus)
        focus_toplevel(toplevel);
    else
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
    if (fullscreen)
        set_fullscreen_focus(toplevel, true, visible);
    else if (ruled && rule.maximize)
        place_by_hand(toplevel, SH_MAXIMIZE);
    else if (maximized)
        maximize_toplevel(toplevel, true);
#if WLR_HAS_XWAYLAND
    // An X11 client may have asked for attention before it mapped; if it opens without focus,
    // that request stands (the policy is applied as if it had come after).
    if (!focus && toplevel->xsurface) {
        const xcb_icccm_wm_hints_t *hints = toplevel->xsurface->hints;
        toplevel->x_hint_urgent = hints && (hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY);
        if (toplevel->x_hint_urgent || toplevel->xsurface->demands_attention)
            activation_requested(toplevel);
    }
#endif
    // The first frame is already committed and shows at once, only faded and a little small.
    toplevel->shown = true;
    struct wlr_box box = toplevel_geometry(toplevel);
    sh_anim_open(server->animator, &toplevel->anim, toplevel->content, box.width / 2.0,
                 box.height / 2.0);
    notify_subscribers(server); // its workspace holds a window now
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    toplevel->fullscreen_cover = toplevel->xdg_toplevel->requested.fullscreen;
    map_toplevel(toplevel, toplevel->xdg_toplevel->requested.fullscreen,
                 toplevel->xdg_toplevel->requested.maximized);
}

static void unmap_toplevel(struct sh_toplevel *toplevel) {
    // The client's buffers go with this commit; the closing animation draws a copy of them.
    struct sh_server *server = toplevel->server;
    if (toplevel->shown && server->running && toplevel_visible(toplevel)) {
        struct wlr_box box = toplevel_geometry(toplevel);
        sh_anim_close(server->animator, &toplevel->scene_tree->node, toplevel->content,
                      box.width / 2.0, box.height / 2.0);
    }
    sh_anim_finish(&toplevel->anim);
    toplevel->shown = false;
    swallow_end(toplevel);
    if (toplevel == toplevel->server->grabbed_toplevel) {
        reset_cursor_mode(toplevel->server);
    }
    forget_decoration(toplevel);
    group_detach(toplevel);

    switcher_forget(toplevel);
    overview_forget(toplevel);
    toplevel->fullscreen = toplevel->fullscreen_cover = false;
    toplevel->scratchpad = false;
    toplevel->urgent = false;
    refresh_frame(toplevel);
    untile_toplevel(toplevel, false);
    bool was_focused = toplevel->server->focused_toplevel == toplevel;
    if (was_focused)
        deactivate_toplevel(toplevel->server);
    unpublish_toplevel(toplevel);
    wl_list_remove(&toplevel->link);
    if (was_focused)
        focus_previous(toplevel->server);
    notify_subscribers(toplevel->server);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    unmap_toplevel(toplevel);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        int width = 0, height = 0;
        toplevel->tile_sized = initial_tile_size(toplevel, &width, &height);
        if (toplevel->tile_sized)
            toplevel_set_states(toplevel, false, ALL_EDGES);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
        if (toplevel->decoration)
            wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                                                    decoration_mode(toplevel->decoration));
    } else if (toplevel->xdg_toplevel->base->surface->mapped) {
        uint64_t started = now_ns();
        refresh_frame(toplevel);
        ++toplevel->server->stats.commits;
        toplevel->server->stats.commit_ns += now_ns() - started;
    }
}

/* Frees a window after removing the listeners xdg-shell and X11 windows have in common. */
static void free_toplevel(struct sh_toplevel *toplevel) {
    sh_anim_finish(&toplevel->anim);
    sh_tween_stop(&toplevel->fade);
    free(toplevel->opacity_rule.app_id);
    free(toplevel->opacity_rule.title);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    wl_list_remove(&toplevel->request_minimize.link);
    wl_list_remove(&toplevel->title_changed.link);
    wl_list_remove(&toplevel->app_id_changed.link);
    free(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    overview_forget(toplevel);
    // The decoration outlives this listener: it hears the same signal later.
    if (toplevel->decoration) {
        wl_list_remove(&toplevel->decoration_mode.link);
        wl_list_remove(&toplevel->decoration_destroy.link);
    }
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    sh_anim_finish(&toplevel->anim);
    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    free_toplevel(toplevel);
}

static void begin_interactive(struct sh_toplevel *toplevel, enum sh_cursor_mode mode,
                              uint32_t edges) {
    struct sh_server *server = toplevel->server;
    if (toplevel->fullscreen) {
        // Only a move, and it leaves fullscreen once the pointer has travelled a little.
        if (mode != SH_CURSOR_MOVE)
            return;
        server->grabbed_toplevel = toplevel;
        server->cursor_mode = mode;
        server->grab_fullscreen = true;
        server->grab_x = server->cursor->x;
        server->grab_y = server->cursor->y;
        return;
    }

    server->grab_output = toplevel_output(toplevel);
    // Resizing a tile moves its splits; moving one lifts it out until it is dropped.
    bool tiled_resize = toplevel->tiled && mode == SH_CURSOR_RESIZE;
    bool retile = toplevel->tiled && mode == SH_CURSOR_MOVE;
    if (retile)
        untile_toplevel(toplevel, false);
    bool was_arranged = toplevel->arranged;
    if (!tiled_resize) {
        toplevel->arranged = false;
        toplevel_set_states(toplevel, false, 0);
    }
    server->grab_retile = retile;
    server->grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == SH_CURSOR_MOVE && was_arranged) {
        /* Dragging a maximized or snapped window restores its floating size, keeping the
         * pointer at the same relative spot across the width and at most as far down. */
        struct wlr_box geometry = toplevel_geometry(toplevel);
        struct wlr_box restore = toplevel->restore_box;
        if (restore.width <= 0 || restore.height <= 0) { // never floated: keep its size
            restore.width = geometry.width;
            restore.height = geometry.height;
        }
        double from_left = server->cursor->x - toplevel->scene_tree->node.x;
        double from_top = server->cursor->y - toplevel->scene_tree->node.y;
        if (geometry.width > 0)
            from_left = from_left * restore.width / geometry.width;
        if (from_top > restore.height)
            from_top = restore.height / 2.0;
        toplevel_configure(toplevel, server->cursor->x - from_left, server->cursor->y - from_top,
                           restore.width, restore.height);
    }

    if (mode == SH_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    } else {
        struct wlr_box geo_box = toplevel_geometry(toplevel);

        edges = corner_edges(toplevel, edges);

        double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
                          ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
                          ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        server->grab_geobox = geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}

/* Client decorations often live in subsurfaces (kitty's title bar), so the clicked surface
 * only has to belong to the toplevel, not be its root surface. */
static bool validate_grab_serial(struct sh_toplevel *toplevel, uint32_t serial) {
    struct wlr_seat *seat = toplevel->server->seat;
    struct wlr_surface *focused = seat->pointer_state.focused_surface;
    return wlr_seat_validate_pointer_grab_serial(seat, NULL, serial) && focused &&
           wlr_surface_get_root_surface(focused) == toplevel->xdg_toplevel->base->surface;
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    struct wlr_xdg_toplevel_move_event *event = data;
    if (validate_grab_serial(toplevel, event->serial))
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_resize_event *event = data;
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    if (validate_grab_serial(toplevel, event->serial))
        begin_interactive(toplevel, SH_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (!toplevel->xdg_toplevel->base->initialized)
        return;
    if (!toplevel->fullscreen)
        maximize_toplevel(toplevel, toplevel->xdg_toplevel->requested.maximized);
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void fit_fullscreen(struct sh_toplevel *toplevel) {
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    toplevel_configure_box(toplevel, fullscreen_box(toplevel, output));
}

static void refit_fullscreen(struct sh_server *server) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen)
            fit_fullscreen(toplevel);
    }
}

static void set_fullscreen(struct sh_toplevel *toplevel, bool fullscreen) {
    set_fullscreen_focus(toplevel, fullscreen, true);
}
/* As set_fullscreen; entering fullscreen focuses the window only with `focus`. */
static void set_fullscreen_focus(struct sh_toplevel *toplevel, bool fullscreen, bool focus) {
    struct sh_server *server = toplevel->server;
    if (!toplevel_mapped(toplevel) || toplevel->fullscreen == fullscreen) {
        toplevel_refresh(toplevel);
        return;
    }
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    int from_x = node->x, from_y = node->y;
    if (fullscreen)
        toplevel->fullscreen_restore = toplevel_box(toplevel);
    toplevel->fullscreen = fullscreen;
    if (!fullscreen)
        toplevel->fullscreen_cover = false;
    toplevel_set_fullscreen_state(toplevel, fullscreen);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign, fullscreen);
    if (fullscreen) {
        fit_fullscreen(toplevel);
    } else {
        toplevel->fullscreen_restore =
            rebase_box(server, toplevel->fullscreen_restore, toplevel_output(toplevel));
        toplevel_configure_box(toplevel, toplevel->fullscreen_restore);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server->windows);
        // The usable area may have changed while this window covered the output.
        struct wlr_output *output =
            toplevel->tiled ? tiled_output(toplevel) : toplevel_output(toplevel);
        if ((toplevel->arranged || toplevel->tiled) && output)
            reflow_output(server, output);
    }
    // The window glides from where it was drawn; the size follows when the client draws it.
    if (toplevel->shown && toplevel_visible(toplevel))
        sh_anim_glide_kind(server->animator, &toplevel->anim, toplevel->content,
                           from_x - node->x, from_y - node->y, SH_ANIM_FULLSCREEN);
    if (server->focused_toplevel == toplevel || (fullscreen && focus))
        focus_toplevel(toplevel);
    refresh_decoration(toplevel);
    refresh_frame(toplevel);
}

/* Fullscreen the client asks for itself, like a video player's: it covers the panels too. */
static void set_client_fullscreen(struct sh_toplevel *toplevel, bool fullscreen) {
    if (fullscreen && !toplevel->fullscreen)
        toplevel->fullscreen_cover = true;
    set_fullscreen(toplevel, fullscreen);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    set_client_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct sh_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;

    // Listen before the scene does, so the surfaces are still shown when the window unmaps
    // and the closing animation can copy them.
    struct wlr_surface *surface = xdg_toplevel->base->surface;
    add_listener(&surface->events.map, &toplevel->map, xdg_toplevel_map);
    add_listener(&surface->events.unmap, &toplevel->unmap, xdg_toplevel_unmap);
    add_listener(&surface->events.commit, &toplevel->commit, xdg_toplevel_commit);
    toplevel->scene_tree = wlr_scene_tree_create(toplevel->server->windows);
    toplevel->content = wlr_scene_tree_create(toplevel->scene_tree);
    wlr_scene_xdg_surface_create(toplevel->content, xdg_toplevel->base);
    toplevel->node = (struct sh_node){SH_NODE_TOPLEVEL, toplevel};
    toplevel->scene_tree->node.data = &toplevel->node;
    toplevel->content->node.data = &toplevel->node;
    xdg_toplevel->base->data = toplevel->scene_tree;
    add_listener(&xdg_toplevel->events.destroy, &toplevel->destroy, xdg_toplevel_destroy);
    add_listener(&xdg_toplevel->events.set_title, &toplevel->title_changed, toplevel_title_changed);
    add_listener(&xdg_toplevel->events.set_app_id, &toplevel->app_id_changed,
                 toplevel_app_id_changed);
    add_listener(&xdg_toplevel->events.request_move, &toplevel->request_move,
                 xdg_toplevel_request_move);
    add_listener(&xdg_toplevel->events.request_resize, &toplevel->request_resize,
                 xdg_toplevel_request_resize);
    add_listener(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize,
                 xdg_toplevel_request_maximize);
    add_listener(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen,
                 xdg_toplevel_request_fullscreen);
    add_listener(&xdg_toplevel->events.request_minimize, &toplevel->request_minimize,
                 toplevel_request_minimize);
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

static void server_new_decoration(struct wl_listener *listener, void *data) {
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

#if WLR_HAS_XWAYLAND
/* X11 windows: managed ones behave like xdg toplevels; override-redirect ones
 * (menus, tooltips, drag icons) are drawn where they ask and never take part in focus order. */
static void xwayland_map(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    struct wlr_xwayland_surface *xsurface = toplevel->xsurface;
    struct sh_server *server = toplevel->server;
    toplevel->unmanaged = xsurface->override_redirect;
    toplevel->scene_tree =
        wlr_scene_tree_create(toplevel->unmanaged ? server->unmanaged : server->windows);
    toplevel->content = toplevel->scene_tree ? wlr_scene_tree_create(toplevel->scene_tree) : NULL;
    if (!toplevel->content ||
        !wlr_scene_subsurface_tree_create(toplevel->content, xsurface->surface)) {
        wlr_log(WLR_ERROR, "Cannot create scene for X11 window");
        if (toplevel->scene_tree)
            wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree = toplevel->content = NULL;
        toplevel->dim = NULL;
        return;
    }
    if (toplevel->unmanaged) {
        wlr_scene_node_set_position(&toplevel->scene_tree->node, xsurface->x, xsurface->y);
        if (!server->locked && wlr_xwayland_surface_override_redirect_wants_focus(xsurface))
            keyboard_enter(server->seat, xsurface->surface);
        return;
    }
    toplevel->scene_tree->node.data = &toplevel->node;
    toplevel->content->node.data = &toplevel->node;
    toplevel->fullscreen_cover = xsurface->fullscreen;
    map_toplevel(toplevel, xsurface->fullscreen,
                 xsurface->maximized_horz && xsurface->maximized_vert);
    refresh_decoration(toplevel);
}

static void xwayland_unmap(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (toplevel->unmanaged) {
        // Return the keyboard from a closed X11 menu to the focused window.
        if (server->seat->keyboard_state.focused_surface == toplevel->xsurface->surface) {
            if (server->focused_toplevel)
                focus_toplevel(server->focused_toplevel);
            else
                wlr_seat_keyboard_clear_focus(server->seat);
        }
    } else {
        unmap_toplevel(toplevel);
    }
    sh_anim_finish(&toplevel->anim);
    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    toplevel->scene_tree = toplevel->content = NULL;
    toplevel->dim = NULL;
}

static void xwayland_associate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_associate);
    struct wlr_surface *surface = toplevel->xsurface->surface;
    toplevel->associated = true;
    add_listener(&surface->events.map, &toplevel->map, xwayland_map);
    add_listener(&surface->events.unmap, &toplevel->unmap, xwayland_unmap);
    // The X11 and Wayland sockets race: Xwayland's first buffer can arrive before the
    // WL_SURFACE_SERIAL message that pairs it, and wlroots only maps on a later commit. An
    // unmapped surface gets no frame callbacks, so Xwayland never sends one (Wine dialogs).
    if (!surface->mapped && wlr_surface_has_buffer(surface))
        wlr_surface_map(surface);
}

static void xwayland_dissociate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_dissociate);
    toplevel->associated = false;
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
}

static void xwayland_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    if (toplevel->associated) {
        wl_list_remove(&toplevel->map.link);
        wl_list_remove(&toplevel->unmap.link);
    }
    wl_list_remove(&toplevel->x_associate.link);
    wl_list_remove(&toplevel->x_dissociate.link);
    wl_list_remove(&toplevel->x_configure.link);
    wl_list_remove(&toplevel->x_activate.link);
    wl_list_remove(&toplevel->x_geometry.link);
    wl_list_remove(&toplevel->x_decorations.link);
    wl_list_remove(&toplevel->x_attention.link);
    wl_list_remove(&toplevel->x_hints.link);
    free_toplevel(toplevel);
}

static bool xwayland_managed(struct sh_toplevel *toplevel) {
    return toplevel_mapped(toplevel) && !toplevel->unmanaged;
}

static void xwayland_set_decorations(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_decorations);
    if (xwayland_managed(toplevel))
        refresh_decoration(toplevel);
}

static void xwayland_request_configure(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (!xwayland_managed(toplevel)) {
        wlr_xwayland_surface_configure(toplevel->xsurface, event->x, event->y, event->width,
                                       event->height);
        if (toplevel->scene_tree)
            wlr_scene_node_set_position(&toplevel->scene_tree->node, event->x, event->y);
        return;
    }
    // Placement belongs to the compositor; floating windows may still choose their size.
    if (toplevel->fullscreen || toplevel->arranged || toplevel->tiled) {
        toplevel_refresh(toplevel);
        return;
    }
    toplevel_configure(toplevel, toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
                       event->width, event->height);
}

static void xwayland_set_geometry(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_geometry);
    if (toplevel->unmanaged && toplevel->scene_tree)
        wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->xsurface->x,
                                    toplevel->xsurface->y);
    else if (xwayland_managed(toplevel))
        refresh_frame(toplevel);
}

static void xwayland_request_activate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_activate);
    if (xwayland_managed(toplevel))
        activation_requested(toplevel);
}

/* _NET_WM_STATE_DEMANDS_ATTENTION and the urgency flag of WM_HINTS ask for attention the way
 * xdg-activation does; a client clears them (or the window is focused) when it is done. */
static void xwayland_attention(struct sh_toplevel *toplevel, bool wanted) {
    if (!xwayland_managed(toplevel))
        return;
    if (!wanted)
        set_urgent(toplevel, false);
    else if (toplevel->server->focused_toplevel != toplevel)
        activation_requested(toplevel);
}

static void xwayland_demands_attention(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_attention);
    xwayland_attention(toplevel, toplevel->xsurface->demands_attention);
}

static void xwayland_set_hints(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_hints);
    const xcb_icccm_wm_hints_t *hints = toplevel->xsurface->hints;
    bool urgent = hints && (hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY);
    if (urgent == toplevel->x_hint_urgent)
        return;
    toplevel->x_hint_urgent = urgent;
    xwayland_attention(toplevel, urgent);
}

/* X11 grab requests carry no serial; accept them only while a button is held. */
static void xwayland_request_move(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if (xwayland_managed(toplevel) && toplevel->server->seat->pointer_state.button_count > 0)
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
}

static void xwayland_request_resize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    struct wlr_xwayland_resize_event *event = data;
    if (xwayland_managed(toplevel) && toplevel->server->seat->pointer_state.button_count > 0)
        begin_interactive(toplevel, SH_CURSOR_RESIZE, event->edges);
}

static void xwayland_request_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (!xwayland_managed(toplevel) || toplevel->fullscreen)
        return;
    maximize_toplevel(toplevel,
                      toplevel->xsurface->maximized_horz || toplevel->xsurface->maximized_vert);
}

static void xwayland_request_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    if (xwayland_managed(toplevel))
        set_client_fullscreen(toplevel, toplevel->xsurface->fullscreen);
}

static void xwayland_request_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
    struct wlr_xwayland_minimize_event *event = data;
    if (!xwayland_managed(toplevel))
        return;
    if (event->minimize)
        minimize_toplevel(toplevel);
    else
        focus_toplevel(toplevel);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    struct sh_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        wlr_xwayland_surface_close(xsurface);
        return;
    }
    toplevel->server = server;
    toplevel->xsurface = xsurface;
    toplevel->node = (struct sh_node){SH_NODE_TOPLEVEL, toplevel};
    xsurface->data = toplevel;
    add_listener(&xsurface->events.associate, &toplevel->x_associate, xwayland_associate);
    add_listener(&xsurface->events.dissociate, &toplevel->x_dissociate, xwayland_dissociate);
    add_listener(&xsurface->events.destroy, &toplevel->destroy, xwayland_destroy);
    add_listener(&xsurface->events.request_configure, &toplevel->x_configure,
                 xwayland_request_configure);
    add_listener(&xsurface->events.request_activate, &toplevel->x_activate,
                 xwayland_request_activate);
    add_listener(&xsurface->events.set_geometry, &toplevel->x_geometry, xwayland_set_geometry);
    add_listener(&xsurface->events.set_decorations, &toplevel->x_decorations,
                 xwayland_set_decorations);
    add_listener(&xsurface->events.request_demands_attention, &toplevel->x_attention,
                 xwayland_demands_attention);
    add_listener(&xsurface->events.set_hints, &toplevel->x_hints, xwayland_set_hints);
    add_listener(&xsurface->events.set_title, &toplevel->title_changed, toplevel_title_changed);
    add_listener(&xsurface->events.set_class, &toplevel->app_id_changed, toplevel_app_id_changed);
    add_listener(&xsurface->events.request_move, &toplevel->request_move, xwayland_request_move);
    add_listener(&xsurface->events.request_resize, &toplevel->request_resize,
                 xwayland_request_resize);
    add_listener(&xsurface->events.request_maximize, &toplevel->request_maximize,
                 xwayland_request_maximize);
    add_listener(&xsurface->events.request_fullscreen, &toplevel->request_fullscreen,
                 xwayland_request_fullscreen);
    add_listener(&xsurface->events.request_minimize, &toplevel->request_minimize,
                 xwayland_request_minimize);
}

#if SHAODESK_XWM_WAKER
/* wlroots' XWM can strand X events: xcb reads them into its queue during flushes
 * and round-trips outside the event handler, and the handler's post-dispatch
 * check ignores that queue (packaging/patches/wlroots-xwm-drain.patch fixes it).
 * That strands the first MapRequest after Xwayland starts, among others. While
 * Xwayland runs, a periodic client message from a separate connection, sent only
 * to the XWM's own window, makes its socket readable so the handler drains the queue. */
enum { XWM_WAKE_INTERVAL_MS = 250 };

static void close_xwm_waker(struct sh_server *server) {
    if (server->waker_timer)
        wl_event_source_remove(server->waker_timer);
    if (server->waker_input)
        wl_event_source_remove(server->waker_input);
    if (server->xwm_waker)
        xcb_disconnect(server->xwm_waker);
    server->waker_timer = server->waker_input = NULL;
    server->xwm_waker = NULL;
}

static int xwm_waker_tick(void *data) {
    struct sh_server *server = data;
    xcb_client_message_event_t message = {.response_type = XCB_CLIENT_MESSAGE,
                                          .format = 32,
                                          .window = server->xwm_window,
                                          .type = server->waker_atom};
    // An empty event mask delivers the message only to the window's creator: the XWM.
    xcb_send_event(server->xwm_waker, false, server->xwm_window, XCB_EVENT_MASK_NO_EVENT,
                   (const char *)&message);
    xcb_flush(server->xwm_waker);
    wl_event_source_timer_update(server->waker_timer, XWM_WAKE_INTERVAL_MS);
    return 0;
}

static int xwm_waker_input(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(server->xwm_waker)))
        free(event); // Only errors can arrive; no events are selected.
    if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) || xcb_connection_has_error(server->xwm_waker))
        close_xwm_waker(server);
    return 0;
}

static void open_xwm_waker(struct sh_server *server) {
    close_xwm_waker(server);
    server->xwm_waker = xcb_connect(server->xwayland->display_name, NULL);
    if (xcb_connection_has_error(server->xwm_waker)) {
        xcb_disconnect(server->xwm_waker);
        server->xwm_waker = NULL;
        wlr_log(WLR_ERROR, "Cannot connect XWM waker; X11 windows may appear late");
        return;
    }
    xcb_atom_t atoms[2] = {XCB_ATOM_NONE, XCB_ATOM_NONE};
    const char *names[2] = {"_SHAODESK_XWM_WAKE", "_NET_SUPPORTING_WM_CHECK"};
    for (int i = 0; i < 2; ++i) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
            server->xwm_waker,
            xcb_intern_atom(server->xwm_waker, false, strlen(names[i]), names[i]), NULL);
        if (reply)
            atoms[i] = reply->atom;
        free(reply);
    }
    server->waker_atom = atoms[0];
    // Like the XWM's connection, this one must not keep an idle Xwayland running.
    xcb_xfixes_query_version_reply_t *xfixes = xcb_xfixes_query_version_reply(
        server->xwm_waker, xcb_xfixes_query_version(server->xwm_waker, 6, 0), NULL);
    if (xfixes && xfixes->major_version >= 6)
        xcb_xfixes_set_client_disconnect_mode(server->xwm_waker,
                                              XCB_XFIXES_CLIENT_DISCONNECT_FLAGS_TERMINATE);
    free(xfixes);
    server->xwm_window = XCB_WINDOW_NONE;
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(server->xwm_waker)).data;
    xcb_get_property_reply_t *check = xcb_get_property_reply(
        server->xwm_waker,
        xcb_get_property(server->xwm_waker, false, screen->root, atoms[1], XCB_ATOM_WINDOW, 0, 1),
        NULL);
    if (check && xcb_get_property_value_length(check) == sizeof(xcb_window_t))
        server->xwm_window = *(xcb_window_t *)xcb_get_property_value(check);
    free(check);
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    server->waker_input = wl_event_loop_add_fd(loop, xcb_get_file_descriptor(server->xwm_waker),
                                               WL_EVENT_READABLE, xwm_waker_input, server);
    server->waker_timer = wl_event_loop_add_timer(loop, xwm_waker_tick, server);
    if (server->waker_atom == XCB_ATOM_NONE || server->xwm_window == XCB_WINDOW_NONE ||
        !server->waker_input || !server->waker_timer) {
        wlr_log(WLR_ERROR, "Cannot set up XWM waker; X11 windows may appear late");
        close_xwm_waker(server);
        return;
    }
    xwm_waker_tick(server); // drain whatever the XWM stranded while attaching
}
#endif

static void xwayland_ready(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, xwayland_ready);
    wlr_log(WLR_INFO, "XWayland ready on DISPLAY=%s", server->xwayland->display_name);
    wlr_xwayland_set_seat(server->xwayland, server->seat);
#if SHAODESK_XWM_WAKER
    open_xwm_waker(server);
#endif
    if (wlr_xcursor_manager_load(server->cursor_mgr, 1)) {
        struct wlr_xcursor *xcursor =
            wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
        if (xcursor) {
            struct wlr_xcursor_image *image = xcursor->images[0];
            wlr_xwayland_set_cursor(server->xwayland, wlr_xcursor_image_get_buffer(image),
                                    image->hotspot_x, image->hotspot_y);
        }
    }
}
#endif

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    struct sh_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        // Keep menus on the output of their window or panel; positioners say how to flip or slide.
        struct wlr_scene_tree *root = popup->xdg_popup->base->data;
        while (root && !root->node.data)
            root = root->node.parent;
        struct sh_server *server = popup->server;
        int root_x = 0, root_y = 0;
        if (root)
            wlr_scene_node_coords(&root->node, &root_x, &root_y);
        else
            root_x = server->cursor->x, root_y = server->cursor->y;
        struct wlr_output *output =
            wlr_output_layout_output_at(server->output_layout, root_x, root_y);
        if (!output)
            output = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
                                                 server->cursor->y);
        if (root && output) {
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output, &box);
            box.x -= root_x;
            box.y -= root_y;
            wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &box);
        }
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    struct sh_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

static void create_popup(struct sh_server *server, struct wlr_xdg_popup *xdg_popup,
                         struct wlr_scene_tree *parent_tree) {
    struct sh_popup *popup = calloc(1, sizeof(*popup));
    popup->server = server;
    popup->xdg_popup = xdg_popup;
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    add_listener(&xdg_popup->base->surface->events.commit, &popup->commit, xdg_popup_commit);
    add_listener(&xdg_popup->events.destroy, &popup->destroy, xdg_popup_destroy);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;
    // A layer-shell popup is attached by the layer's new_popup handler instead.
    if (!popup->parent)
        return;
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (parent && parent->data)
        create_popup(server, popup, parent->data);
}

/* Control socket: one newline-terminated request per connection, answered with
 * "ok\n" plus any output, or "error: ...\n". Lives in the private runtime dir. */
struct sh_control_client {
    struct sh_server *server;
    int fd;
    struct wl_event_source *source;
    bool subscribed; // "subscribe": stays open and receives the state after each change
    struct wl_list link;
    size_t length;
    char request[512];
};

static void control_reply(int fd, const char *text) {
    size_t length = strlen(text);
    while (length > 0) {
        ssize_t written = send(fd, text, length, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return;
        text += written;
        length -= (size_t)written;
    }
}

/* workspace, focused, minimized, tiled, x, y, width, height, app_id, title, output,
 * visible, scratchpad, sticky, group (0 for none) — one line. A window hidden in the
 * scratchpad is minimized. */
static void control_describe_window(struct sh_server *server, int fd,
                                    struct sh_toplevel *toplevel) {
    char line[1024], app_id[256], title[512];
    const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
    snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
    snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
    // Neither can break the columns.
    for (char *c = app_id; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
    for (char *c = title; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
    struct wlr_box geometry = toplevel_geometry(toplevel);
    snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%d\t%u\n",
             toplevel->workspace + 1, server->focused_toplevel == toplevel, toplevel->minimized,
             toplevel->tiled, toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
             geometry.width, geometry.height, app_id, title, toplevel->output,
             toplevel_visible(toplevel), toplevel->scratchpad, toplevel->sticky,
             toplevel->group);
    control_reply(fd, line);
}

static void control_describe_windows(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link)
        control_describe_window(server, fd, toplevel);
}

/* The urgent windows in the columns of `get windows`, the one that has waited longest first. */
static void control_describe_urgent(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    unsigned last = 0;
    for (;;) {
        struct sh_toplevel *toplevel, *next = NULL;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->urgent_order > last &&
                (!next || toplevel->urgent_order < next->urgent_order))
                next = toplevel;
        }
        if (!next)
            return;
        last = next->urgent_order;
        control_describe_window(server, fd, next);
    }
}

/* The workspaces of `output` that hold windows, as "1,3", or "-" for none. */
static void occupied_workspaces(struct sh_server *server, struct wlr_output *output, char *text,
                                size_t size) {
    unsigned used = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->scratchpad && toplevel->minimized)
            continue; // hidden in the scratchpad, on no workspace
        if (!strcmp(toplevel->output, output->name) && toplevel->workspace < 32)
            used |= 1u << toplevel->workspace;
    }
    size_t length = 0;
    text[0] = '\0';
    for (int i = 0; i < 32 && length < size; ++i) {
        if (used & 1u << i)
            length += snprintf(text + length, size - length, "%s%d", length ? "," : "", i + 1);
    }
    if (!used)
        snprintf(text, size, "-");
}

/* The focused output's workspace, numbered from 1, or 1 without outputs. */
static int focused_workspace(struct sh_server *server) {
    struct wlr_output *output = focused_output(server);
    return output ? *output_workspace(server, output->name) + 1 : 1;
}

static void control_describe_layers(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_layer *layer;
    // namespace, output, layer (0 background to 3 overlay), shown — one per line.
    wl_list_for_each_reverse(layer, &server->layers, link) {
        struct wlr_layer_surface_v1 *surface = layer->surface;
        char namespace[256], line[512];
        snprintf(namespace, sizeof(namespace), "%s", surface->namespace);
        for (char *c = namespace; *c; ++c)
            if (*c == '\n' || *c == '\r' || *c == '\t')
                *c = ' ';
        snprintf(line, sizeof(line), "%s\t%s\t%d\t%d\n", namespace,
                 surface->output ? surface->output->name : "", surface->current.layer,
                 surface->surface->mapped && layer->scene->tree->node.enabled);
        control_reply(fd, line);
    }
}

static void control_describe_output(struct sh_server *server, int fd, struct sh_output *output) {
    struct wlr_output *o = output->wlr_output;
    struct wlr_box box = {0};
    if (!output->disabled)
        wlr_output_layout_get_box(server->output_layout, o, &box);
    char line[512], description[256];
    output_description(o, description, sizeof(description));
    // name, enabled, x, y, logical width, height, scale, transform, mode, description.
    snprintf(line, sizeof(line), "%s\t%d\t%d\t%d\t%d\t%d\t%g\t%d\t%dx%d@%.3f\t%s\n", o->name,
             !output->disabled, box.x, box.y, box.width, box.height, o->scale, o->transform,
             o->width, o->height, o->refresh / 1000.0, description);
    control_reply(fd, line);
}

/* Sessions: `session save NAME` writes what every output and window is doing to a file (see
 * shaodesk/session.h); `session restore NAME [launch]` puts matching windows back, and with
 * `launch` starts the applications that are missing, placing their windows as they open. */
static pid_t toplevel_pid(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->pid;
#endif
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(toplevel->xdg_toplevel->resource), &pid,
                              NULL, NULL);
    return pid;
}

static void session_read_command(struct sh_toplevel *toplevel, struct sh_session_window *window) {
    pid_t pid = toplevel_pid(toplevel);
    window->command[0] = '\0';
    if (pid <= 1 || pid == getpid())
        return;
    char path[64], cmdline[4096];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    size_t length = fread(cmdline, 1, sizeof(cmdline), file);
    fclose(file);
    if (length < sizeof(cmdline))
        sh_session_set_command(window, cmdline, length);
}

static void session_capture(struct sh_server *server, struct sh_session *session) {
    const struct sh_settings *settings = server_settings(server);
    memset(session, 0, sizeof(*session));
    for (size_t i = 0; i < sizeof(server->output_workspaces) / sizeof(*server->output_workspaces) &&
                       session->output_count < SH_SESSION_MAX_OUTPUTS;
         ++i) {
        const char *name = server->output_workspaces[i].name;
        if (!name[0])
            continue;
        struct sh_session_output *o = &session->outputs[session->output_count++];
        snprintf(o->name, sizeof(o->name), "%s", name);
        o->workspace = server->output_workspaces[i].current;
        struct wlr_output *output = find_output(server, name);
        o->tiling = output ? output_tiles(server, output) : server->output_workspaces[i].tiling;
        for (int workspace = 0; workspace < settings->workspaces &&
                                session->layout_count < SH_SESSION_MAX_LAYOUTS;
             ++workspace) {
            enum sh_tile_layout layout = sh_tiling_layout(server->tiling, name, workspace);
            double ratio = sh_tiling_ratio(server->tiling, name, workspace);
            int count = sh_tiling_master_count(server->tiling, name, workspace);
            enum sh_tile_layout default_layout;
            double default_ratio;
            int default_count;
            sh_tiling_output_defaults(server->tiling, name, &default_layout, &default_ratio,
                                      &default_count);
            double widths[SH_SESSION_MAX_COLUMNS];
            int width_count = sh_tiling_scroll_widths(server->tiling, name, workspace, widths,
                                                      SH_SESSION_MAX_COLUMNS);
            if (layout == default_layout && fabs(ratio - default_ratio) < 0.0001 &&
                count == default_count && width_count == 0)
                continue;
            struct sh_session_layout *l = &session->layouts[session->layout_count++];
            memcpy(l->widths, widths, sizeof(double) * (size_t)width_count);
            l->width_count = width_count;
            snprintf(l->output, sizeof(l->output), "%s", name);
            l->workspace = workspace;
            l->layout = layout;
            l->ratio = ratio;
            l->master_count = count;
        }
    }
    // Oldest first, so that restoring in this order ends with the newest on top.
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (!toplevel_mapped(toplevel) || toplevel->group_hidden || toplevel->swallowed || session->window_count >= SH_SESSION_MAX_WINDOWS)
            continue;
        struct sh_session_window *w = &session->windows[session->window_count++];
        const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
        snprintf(w->output, sizeof(w->output), "%s", toplevel->output);
        w->workspace = toplevel->workspace;
        w->flags = (toplevel->tiled ? SH_SESSION_TILED : 0) |
                   (toplevel->floating ? SH_SESSION_FLOATING : 0) |
                   (toplevel->minimized && !toplevel->scratchpad ? SH_SESSION_MINIMIZED : 0) |
                   (toplevel->sticky ? SH_SESSION_STICKY : 0) |
                   (toplevel->fullscreen ? SH_SESSION_FULLSCREEN : 0) |
                   (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE && !toplevel->tiled
                        ? SH_SESSION_MAXIMIZED
                        : 0) |
                   (toplevel->scratchpad ? SH_SESSION_SCRATCHPAD : 0) |
                   (server->focused_toplevel == toplevel ? SH_SESSION_FOCUSED : 0);
        // A tile is saved with the place it floats at; a window that has none floats on its tile.
        struct wlr_box box = toplevel_box(toplevel);
        if (toplevel->tiled && toplevel->restore_box.width > 0 && toplevel->restore_box.height > 0)
            box = toplevel->restore_box;
        else if (toplevel->fullscreen && toplevel->fullscreen_restore.width > 0)
            box = toplevel->fullscreen_restore;
        else if (toplevel->arranged && toplevel->restore_box.width > 0 &&
                 toplevel->restore_box.height > 0 && !toplevel->tiled)
            box = toplevel->restore_box;
        w->x = box.x, w->y = box.y, w->width = box.width, w->height = box.height;
        snprintf(w->app_id, sizeof(w->app_id), "%s", app_id ? app_id : "");
        snprintf(w->title, sizeof(w->title), "%s", title ? title : "");
        session_read_command(toplevel, w);
        int row = 0, column = toplevel->tiled ? sh_tiling_scroll_column(server->tiling, toplevel, &row) : -1;
        if (column >= 0) {
            w->scroll_column = column + 1;
            w->scroll_row = row + 1;
        }
    }
}

static bool make_directories(char *path) {
    for (char *c = path + 1; *c; ++c) {
        if (*c != '/')
            continue;
        *c = '\0';
        int result = mkdir(path, 0700);
        *c = '/';
        if (result < 0 && errno != EEXIST)
            return false;
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static bool session_save(struct sh_server *server, const char *name, int *windows, char *error,
                         size_t error_size) {
    char path[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 8];
    if (!sh_session_valid_name(name)) {
        snprintf(error, error_size, "a session name is letters, digits, '.', '_' and '-'");
        return false;
    }
    if (!sh_session_path(NULL, directory, sizeof(directory)) ||
        !sh_session_path(name, path, sizeof(path)) || !make_directories(directory)) {
        snprintf(error, error_size, "cannot create the sessions directory");
        return false;
    }
    struct sh_session *session = malloc(sizeof(*session));
    if (!session) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    session_capture(server, session);
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *file = fopen(temporary, "w");
    bool ok = file && sh_session_write(session, file);
    if (file && fclose(file) != 0)
        ok = false;
    if (ok && rename(temporary, path) != 0)
        ok = false;
    if (!ok) {
        unlink(temporary);
        snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
    }
    *windows = session->window_count;
    free(session);
    return ok;
}

/* Runs a command with an ordinary signal mask (the compositor blocks signals for its event
 * loop); the child is reaped with the others. */
static bool session_spawn(char *const *argv) {
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
        return false;
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t pid;
    extern char **environ;
    int error = posix_spawnp(&pid, argv[0], NULL, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (error)
        wlr_log(WLR_ERROR, "Cannot launch %s: %s", argv[0], strerror(error));
    return error == 0;
}

/* Puts a live window where the session had its match. */
static void session_place(struct sh_server *server, struct sh_toplevel *toplevel,
                          const struct sh_session_window *saved) {
    const struct sh_settings *settings = server_settings(server);
    struct wlr_output *output = find_output(server, saved->output);
    if (!output)
        output = home_output(toplevel);
    if (!output)
        return;
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    if (toplevel->sticky)
        set_sticky(toplevel, false, false);
    toplevel->scratchpad = false;
    untile_toplevel(toplevel, false);
    if (toplevel->arranged)
        unarrange_in_place(toplevel);
    toplevel->minimized = false;
    set_toplevel_output(toplevel, output);
    toplevel->workspace = saved->workspace < settings->workspaces ? saved->workspace
                                                                   : settings->workspaces - 1;
    toplevel->floating = (saved->flags & SH_SESSION_FLOATING) || toplevel_is_dialog(toplevel);
    toplevel->placed = false;
    if ((saved->flags & SH_SESSION_TILED) && !(saved->flags & SH_SESSION_FLOATING) &&
        wants_tiling(toplevel, output)) {
        // The place it floats at, if it is ever floated, is the saved one.
        if (saved->width > 0 && saved->height > 0)
            toplevel->restore_box = (struct wlr_box){saved->x, saved->y, saved->width, saved->height};
        tile_toplevel(toplevel, output, NULL, false);
    } else if (saved->width > 0 && saved->height > 0) {
        struct wlr_box box = rebase_box(
            server, (struct wlr_box){saved->x, saved->y, saved->width, saved->height}, output);
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure_box(toplevel, box);
    }
    if (saved->flags & SH_SESSION_MAXIMIZED)
        place_by_hand(toplevel, SH_MAXIMIZE);
    if (saved->flags & SH_SESSION_STICKY && settings->sticky)
        set_sticky(toplevel, true, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
    if (saved->flags & SH_SESSION_SCRATCHPAD)
        hide_in_scratchpad(toplevel);
    else if (saved->flags & SH_SESSION_MINIMIZED)
        minimize_toplevel(toplevel);
    else if (saved->flags & SH_SESSION_FULLSCREEN)
        set_fullscreen(toplevel, true);
}

/* Puts the tiles of the scrolling layout back into the columns they sat in, with the saved
 * widths. Only windows found now are placed; the ones launched by the restore arrive later and
 * open as usual, right of the focused column. */
static void session_restore_columns(struct sh_server *server, const struct sh_session *session,
                                    struct sh_toplevel *const *live, const int *assignment) {
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < session->layout_count; ++i) {
        const struct sh_session_layout *l = &session->layouts[i];
        struct wlr_output *output = find_output(server, l->output);
        if (!output || l->workspace >= settings->workspaces || l->layout != SH_LAYOUT_SCROLL)
            continue;
        void *windows[SH_SESSION_MAX_WINDOWS];
        int columns[SH_SESSION_MAX_WINDOWS], rows[SH_SESSION_MAX_WINDOWS], count = 0;
        for (int j = 0; j < session->window_count; ++j) {
            const struct sh_session_window *saved = &session->windows[j];
            if (assignment[j] < 0 || saved->scroll_column < 1 || saved->workspace != l->workspace ||
                strcmp(saved->output, l->output) != 0)
                continue;
            struct sh_toplevel *toplevel = live[assignment[j]];
            if (!toplevel->tiled || strcmp(toplevel->output, output->name) != 0)
                continue;
            windows[count] = toplevel;
            columns[count] = saved->scroll_column - 1;
            rows[count++] = saved->scroll_row - 1;
        }
        if (count && sh_tiling_scroll_restore(server->tiling, output->name, l->workspace, windows,
                                              columns, rows, count, l->widths, l->width_count))
            reflow_output(server, output);
    }
}

static bool session_restore(struct sh_server *server, const char *name, bool launch,
                            int *restored, int *launched, int *missing, char *error,
                            size_t error_size) {
    char path[PATH_MAX];
    if (!sh_session_path(name, path, sizeof(path))) {
        snprintf(error, error_size, "a session name is letters, digits, '.', '_' and '-'");
        return false;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        snprintf(error, error_size, "no session named %s", name);
        return false;
    }
    struct sh_session *session = malloc(sizeof(*session));
    bool ok = session && sh_session_read(session, file, error, error_size);
    fclose(file);
    if (!session) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    if (!ok) {
        free(session);
        return false;
    }
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < session->output_count; ++i) {
        struct wlr_output *output = find_output(server, session->outputs[i].name);
        if (output && session->outputs[i].tiling >= 0)
            set_tiling(server, output, session->outputs[i].tiling == 1);
    }
    for (int i = 0; i < session->layout_count; ++i) {
        const struct sh_session_layout *l = &session->layouts[i];
        struct wlr_output *output = find_output(server, l->output);
        if (!output || l->workspace >= settings->workspaces || l->layout >= SH_LAYOUT_COUNT)
            continue;
        sh_tiling_set_layout(server->tiling, output->name, l->workspace,
                             (enum sh_tile_layout)l->layout);
        sh_tiling_adjust(server->tiling, output->name, l->workspace,
                         l->ratio - sh_tiling_ratio(server->tiling, output->name, l->workspace),
                         l->master_count -
                             sh_tiling_master_count(server->tiling, output->name, l->workspace));
        reflow_output(server, output);
    }
    struct sh_toplevel *live[SH_SESSION_MAX_WINDOWS * 2];
    const char *live_app_ids[SH_SESSION_MAX_WINDOWS * 2], *live_titles[SH_SESSION_MAX_WINDOWS * 2];
    int live_count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (!toplevel_mapped(toplevel) || live_count >= SH_SESSION_MAX_WINDOWS * 2)
            continue;
        const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
        live[live_count] = toplevel;
        live_app_ids[live_count] = app_id ? app_id : "";
        live_titles[live_count++] = title ? title : "";
    }
    int assignment[SH_SESSION_MAX_WINDOWS];
    sh_session_match(session->windows, session->window_count, live_app_ids, live_titles,
                     live_count, assignment);
    *restored = *launched = *missing = 0;
    struct sh_toplevel *focus = NULL;
    for (int i = 0; i < session->window_count; ++i) {
        const struct sh_session_window *saved = &session->windows[i];
        if (assignment[i] >= 0) {
            session_place(server, live[assignment[i]], saved);
            if (saved->flags & SH_SESSION_FOCUSED)
                focus = live[assignment[i]];
            ++*restored;
            continue;
        }
        char **argv = launch ? sh_session_argv(saved->command) : NULL;
        size_t slot = 0, slots = sizeof(server->session_pending) / sizeof(*server->session_pending);
        int64_t now = monotonic_ms();
        while (slot < slots && server->session_pending[slot].used &&
               server->session_pending[slot].deadline >= now)
            ++slot;
        if (argv && slot < slots && session_spawn(argv)) {
            server->session_pending[slot].window = *saved;
            server->session_pending[slot].deadline = now + 30000;
            server->session_pending[slot].used = true;
            ++*launched;
        } else {
            ++*missing;
        }
        free(argv);
    }
    session_restore_columns(server, session, live, assignment);
    for (int i = 0; i < session->output_count; ++i) {
        struct wlr_output *output = find_output(server, session->outputs[i].name);
        if (output && session->outputs[i].workspace < settings->workspaces)
            switch_workspace(server, output, session->outputs[i].workspace);
    }
    if (focus && toplevel_visible(focus))
        focus_toplevel(focus);
    notify_subscribers(server);
    free(session);
    return true;
}

static int session_name_compare(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}
static int session_name_filter(const struct dirent *entry) {
    return sh_session_valid_name(entry->d_name);
}

static void control_session(struct sh_server *server, int fd, const char *arguments) {
    char verb[16] = "", name[SH_SESSION_NAME_MAX + 8] = "", option[16] = "", extra[8] = "";
    int count = sscanf(arguments, " %15s %71s %15s %7s", verb, name, option, extra);
    char error[300] = "", reply[512];
    if (!strcmp(verb, "list") && count == 1) {
        char directory[PATH_MAX];
        control_reply(fd, "ok\n");
        struct dirent **entries = NULL;
        int found = sh_session_path(NULL, directory, sizeof(directory))
                        ? scandir(directory, &entries, session_name_filter, session_name_compare)
                        : -1;
        for (int i = 0; i < found; ++i) {
            char path[PATH_MAX];
            struct stat info;
            sh_session_path(entries[i]->d_name, path, sizeof(path));
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                int windows = 0;
                FILE *file = fopen(path, "r");
                char line[8192];
                while (file && fgets(line, sizeof(line), file))
                    windows += !strncmp(line, "window\t", 7);
                if (file)
                    fclose(file);
                snprintf(reply, sizeof(reply), "%s\t%d\t%lld\n", entries[i]->d_name, windows,
                         (long long)info.st_mtime);
                control_reply(fd, reply);
            }
            free(entries[i]);
        }
        free(entries);
        return;
    }
    if (!strcmp(verb, "save") && count == 2) {
        int windows = 0;
        if (session_save(server, name, &windows, error, sizeof(error)))
            snprintf(reply, sizeof(reply), "ok\nsaved %s: %d windows\n", name, windows);
        else
            snprintf(reply, sizeof(reply), "error: %s\n", error);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "restore") && (count == 2 || (count == 3 && !strcmp(option, "launch")))) {
        int restored, launched, missing;
        if (session_restore(server, name, count == 3, &restored, &launched, &missing, error,
                            sizeof(error)))
            snprintf(reply, sizeof(reply), "ok\nrestored %d, launched %d, not found %d\n",
                     restored, launched, missing);
        else
            snprintf(reply, sizeof(reply), "error: %s\n", error);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "delete") && count == 2) {
        char path[PATH_MAX];
        if (!sh_session_path(name, path, sizeof(path)))
            snprintf(reply, sizeof(reply), "error: a session name is letters, digits, '.', '_' and '-'\n");
        else if (unlink(path) != 0)
            snprintf(reply, sizeof(reply), "error: no session named %s\n", name);
        else
            snprintf(reply, sizeof(reply), "ok\n");
        control_reply(fd, reply);
        return;
    }
    control_reply(fd, "error: usage: session save NAME | restore NAME [launch] | list | delete NAME\n");
}

/* An output, enabled or not, by connector name. */
static struct sh_output *sh_output_for_name(struct sh_server *server, const char *name) {
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) {
            if (!strcmp(output->wlr_output->name, name))
                return output;
        }
    }
    return NULL;
}

static void find_headless(struct wlr_backend *backend, void *data) {
    struct wlr_backend **found = data;
    if (wlr_backend_is_headless(backend))
        *found = backend;
}

/* "headless_output add [NAME] [WIDTHxHEIGHT]" plugs in a virtual output, and "headless_output
 * remove NAME" unplugs one, so tests can exercise hotplug without a display. Only under
 * --headless. */
static void control_headless_output(struct sh_server *server, int fd, const char *args) {
    struct wlr_backend *headless = NULL;
    if (wlr_backend_is_headless(server->backend))
        headless = server->backend;
    else if (wlr_backend_is_multi(server->backend))
        wlr_multi_for_each_backend(server->backend, find_headless, &headless);
    if (!headless) {
        control_reply(fd, "error: headless_output needs --headless\n");
        return;
    }
    char verb[16] = "", first[64] = "", second[64] = "";
    int fields = sscanf(args, "%15s %63s %63s", verb, first, second);
    if (!strcmp(verb, "add") && fields >= 1 && fields <= 3) {
        unsigned width = 1280, height = 720;
        char name[64] = "";
        for (int i = 1; i < fields; ++i) {
            const char *token = i == 1 ? first : second;
            char extra;
            if (sscanf(token, "%ux%u%c", &width, &height, &extra) == 2)
                continue;
            if (name[0]) {
                control_reply(fd, "error: usage: headless_output add [NAME] [WIDTHxHEIGHT]\n");
                return;
            }
            snprintf(name, sizeof(name), "%s", token);
        }
        if (!width || !height || width > 16384 || height > 16384) {
            control_reply(fd, "error: bad size\n");
            return;
        }
        if (name[0] && sh_output_for_name(server, name)) {
            control_reply(fd, "error: an output with that name exists\n");
            return;
        }
        snprintf(server->pending_output_name, sizeof(server->pending_output_name), "%s", name);
        struct wlr_output *added = wlr_headless_add_output(headless, width, height);
        server->pending_output_name[0] = '\0';
        if (!added) {
            control_reply(fd, "error: cannot add an output\n");
            return;
        }
        char reply[96];
        snprintf(reply, sizeof(reply), "ok\n%s\n", added->name);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "remove") && fields == 2) {
        struct sh_output *output = sh_output_for_name(server, first);
        if (!output) {
            control_reply(fd, "error: no such output\n");
            return;
        }
        wlr_output_destroy(output->wlr_output);
        control_reply(fd, "ok\n");
        return;
    }
    control_reply(fd, "error: usage: headless_output add [NAME] [WIDTHxHEIGHT] | remove NAME\n");
}

/* "osd TEXT [PERCENT]": shows the shell's on-screen display on the focused output. A last word
 * that is a whole number from 0 to 100, with an optional %, is the level; the shell hears
 * "osd OUTPUT PERCENT TEXT", the percent -1 for none. */
static void control_osd(struct sh_server *server, int fd, const char *arguments) {
    char text[512];
    snprintf(text, sizeof(text), "%s", arguments);
    for (char *c = text; *c; ++c)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
    size_t length = strlen(text);
    while (length && text[length - 1] == ' ')
        text[--length] = '\0';
    char *start = text;
    while (*start == ' ')
        ++start;
    if (!*start) {
        control_reply(fd, "error: usage: osd TEXT [PERCENT]\n");
        return;
    }
    int percent = -1;
    char *last = strrchr(start, ' ');
    if (last) {
        char *end = NULL;
        long value = strtol(last + 1, &end, 10);
        if (end != last + 1 && (!*end || (!strcmp(end, "%"))) && value >= 0 && value <= 100) {
            percent = (int)value;
            while (last > start && last[-1] == ' ')
                --last;
            *last = '\0';
        }
    }
    struct wlr_output *output = focused_output(server);
    char line[640];
    snprintf(line, sizeof(line), "osd %s %d %s\n", output ? output->name : "-", percent, start);
    send_shell_line(server, line);
    control_reply(fd, "ok\n");
}

static void control_handle(struct sh_server *server, int fd, const char *request) {
    if (!strcmp(request, "get outputs")) {
        control_reply(fd, "ok\n");
        struct sh_output *output;
        wl_list_for_each_reverse(output, &server->outputs, link)
            control_describe_output(server, fd, output);
        wl_list_for_each_reverse(output, &server->disabled_outputs, link)
            control_describe_output(server, fd, output);
        return;
    }
    if (!strcmp(request, "get workspace")) {
        char reply[32];
        snprintf(reply, sizeof(reply), "ok\n%d\n", focused_workspace(server));
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get workspaces")) {
        control_reply(fd, "ok\n");
        struct wlr_output *focused = focused_output(server);
        struct sh_output *output;
        // name, current workspace, focused, workspaces with windows, tiling — one line per
        // output.
        wl_list_for_each_reverse(output, &server->outputs, link) {
            char line[256], used[128];
            occupied_workspaces(server, output->wlr_output, used, sizeof(used));
            snprintf(line, sizeof(line), "%s\t%d\t%d\t%s\t%s\n", output->wlr_output->name,
                     *output_workspace(server, output->wlr_output->name) + 1,
                     output->wlr_output == focused, used,
                     output_tiles(server, output->wlr_output) ? "on" : "off");
            control_reply(fd, line);
        }
        return;
    }
    if (!strncmp(request, "get layout", 10) && (!request[10] || request[10] == ' ')) {
        // Layout, master ratio and master count of the focused output's current workspace, or
        // of "get layout OUTPUT [WORKSPACE]" (from 1).
        static const char *const names[] = {"dwindle", "master", "spiral", "monocle", "scroll"};
        struct wlr_output *output = focused_output(server);
        int workspace = output ? *output_workspace(server, output->name) : 0;
        if (request[10]) {
            char name[64];
            int number = 0, fields = sscanf(request + 11, "%63s %d", name, &number);
            output = fields >= 1 ? find_output(server, name) : NULL;
            if (!output || (fields == 2 && (number < 1 || number > server_settings(server)->workspaces))) {
                control_reply(fd, "error: usage: get layout [OUTPUT [WORKSPACE]]\n");
                return;
            }
            workspace = fields == 2 ? number - 1 : *output_workspace(server, output->name);
        }
        char reply[96] = "ok\ndwindle\t0.55\t1\n";
        if (output) {
            snprintf(reply, sizeof(reply), "ok\n%s\t%.2f\t%d\n",
                     names[sh_tiling_layout(server->tiling, output->name, workspace)],
                     sh_tiling_ratio(server->tiling, output->name, workspace),
                     sh_tiling_master_count(server->tiling, output->name, workspace));
        }
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get tiling")) {
        control_reply(fd, output_tiles(server, focused_output(server)) ? "ok\non\n" : "ok\noff\n");
        return;
    }
    if (!strcmp(request, "get urgent")) {
        control_describe_urgent(server, fd);
        return;
    }
    if (!strcmp(request, "get windows")) {
        control_describe_windows(server, fd);
        return;
    }
    if (!strcmp(request, "get swallow")) {
        // Per window, oldest first: app_id, whether it is swallowed (hidden), and the app_id of
        // the window it swallowed or was swallowed by ("-" for none).
        control_reply(fd, "ok\n");
        struct sh_toplevel *toplevel;
        wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
            char line[640];
            const char *app_id = toplevel_app_id(toplevel);
            const char *peer = toplevel->swallow_peer ? toplevel_app_id(toplevel->swallow_peer) : NULL;
            snprintf(line, sizeof(line), "%s\t%d\t%d\t%s\n", app_id && *app_id ? app_id : "-",
                     toplevel->swallowed, toplevel_visible(toplevel),
                     toplevel->swallow_peer ? (peer && *peer ? peer : "?") : "-");
            control_reply(fd, line);
        }
        return;
    }
    if (!strcmp(request, "get guides")) {
        // The magnet guide lines (vertical, then horizontal): shown, x, y, width, height.
        control_reply(fd, "ok\n");
        for (int i = 0; i < 2; ++i) {
            struct wlr_scene_rect *rect = server->guides[i];
            char line[96];
            snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%d\n",
                     rect && rect->node.enabled, rect ? rect->node.x : 0, rect ? rect->node.y : 0,
                     rect ? rect->width : 0, rect ? rect->height : 0);
            control_reply(fd, line);
        }
        return;
    }
    if (!strcmp(request, "get animations")) {
        // Running animations, and the scene trees stacked for windows (with closing copies).
        char reply[64];
        snprintf(reply, sizeof(reply), "ok\n%zu\t%d\t%zu\n", sh_animator_running(server->animator),
                 wl_list_length(&server->windows->children) +
                     wl_list_length(&server->fullscreen->children) +
                     wl_list_length(&server->fullscreen_cover->children),
                 sh_animator_tweens(server->animator));
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get stats")) {
        // frames, their time and the worst (ns), window commits and their time (ns), placements.
        const struct sh_stats *stats = &server->stats;
        char reply[480];
        snprintf(reply, sizeof(reply), "ok\n%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",
                 (unsigned long long)stats->frames, (unsigned long long)stats->frame_ns,
                 (unsigned long long)stats->frame_max_ns, (unsigned long long)stats->commits,
                 (unsigned long long)stats->commit_ns, (unsigned long long)stats->configures,
                 (unsigned long long)stats->opacity_rules, (unsigned long long)stats->motions,
                 (unsigned long long)stats->motion_ns, (unsigned long long)stats->reflows,
                 (unsigned long long)stats->reflow_ns);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get frame_times")) {
        // Total frames so far, then the retained ones (oldest first) as "spent\tinterval" in us.
        const struct sh_stats *stats = &server->stats;
        size_t kept = stats->frames < SH_FRAME_RING ? stats->frames : SH_FRAME_RING;
        char *reply = malloc(64 + kept * 24);
        if (!reply) {
            control_reply(fd, "error\nout of memory\n");
            return;
        }
        int used = snprintf(reply, 64, "ok\n%llu\n", (unsigned long long)stats->frames);
        for (size_t i = 0; i < kept; ++i) {
            size_t slot = (stats->frames - kept + i) % SH_FRAME_RING;
            used += snprintf(reply + used, 24, "%u\t%u\n", stats->frame_us[slot], stats->interval_us[slot]);
        }
        control_reply(fd, reply);
        free(reply);
        return;
    }
    if (!strcmp(request, "get dim")) {
        // Per window, front to back: focused, how opaque its dimming is now, and where that is
        // heading (both in thousandths), and whether a dimming node exists.
        control_reply(fd, "ok\n");
        int64_t now = now_ms();
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            char line[128];
            snprintf(line, sizeof(line), "%d\t%ld\t%ld\t%d\n", server->focused_toplevel == toplevel,
                     lround(1000 * sh_fade_value(&toplevel->dim_fade, now)),
                     lround(1000 * toplevel->dim_fade.to), toplevel->dim != NULL);
            control_reply(fd, line);
        }
        return;
    }
    if (!strcmp(request, "get peek")) {
        // How far windows have faded toward the desktop (thousandths), whether peeking, and
        // whether a held key keeps it up.
        char reply[64];
        snprintf(reply, sizeof(reply), "ok\n%ld\t%d\t%d\n",
                 lround(1000 * sh_fade_value(&server->peek_fade, now_ms())), server->peeking,
                 server->peek_keycode != 0);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get opacities")) {
        // app_id, title, focused, opacity applied to its buffers — one window per line.
        control_reply(fd, "ok\n");
        struct sh_toplevel *toplevel;
        wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
            char line[1024], app_id[256], title[512];
            const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
            snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
            snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
            for (char *c = app_id; *c; ++c)
                *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
            for (char *c = title; *c; ++c)
                *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
            snprintf(line, sizeof(line), "%s\t%s\t%d\t%.3f\n", app_id, title,
                     server->focused_toplevel == toplevel, toplevel->opacity);
            control_reply(fd, line);
        }
        return;
    }
    if (!strcmp(request, "get zoom")) {
        // The magnification now and its target (thousandths), and the number of outputs that
        // drew the last frame magnified.
        int zoomed = 0;
        struct sh_output *output;
        wl_list_for_each(output, &server->outputs, link) zoomed += output->zoomed;
        char reply[64];
        snprintf(reply, sizeof(reply), "ok\n%ld\t%ld\t%d\n",
                 lround(1000 * zoom_level(server, now_ms())), lround(1000 * server->zoom_target),
                 zoomed);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get night_light")) {
        // The temperature applied now, the override (0 schedule, 1 forced neutral, 2 forced
        // warm), and whether the schedule is enabled.
        char reply[64];
        snprintf(reply, sizeof(reply), "ok\n%d\t%d\t%d\n", server->night_kelvin,
                 server->night_mode, server_settings(server)->effects.night_light);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(request, "get overview")) {
        // The state (open, closing or closed) and how far the glide has gone (thousandths),
        // then the overview line and a line per thumbnail and per strip cell, as sent to the
        // shell.
        size_t size = 1024 + (size_t)(OVERVIEW_MAX + OVERVIEW_WORKSPACES) * 512;
        char *text = malloc(size);
        if (!text) {
            control_reply(fd, "error: out of memory\n");
            return;
        }
        control_reply(fd, "ok\n");
        snprintf(text, size, "%s %ld\n",
                 server->overview.open ? "open" : server->overview.visible ? "closing" : "closed",
                 lround(1000 * server->overview.progress));
        control_reply(fd, text);
        if (server->overview.visible) {
            overview_describe(server, text, size);
            control_reply(fd, text);
        }
        free(text);
        return;
    }
    if (!strcmp(request, "get layers")) {
        control_describe_layers(server, fd);
        return;
    }
    if (server->locked) {
        control_reply(fd, "error: the session is locked\n");
        return;
    }
    if (!strncmp(request, "headless_output", 15) && (!request[15] || request[15] == ' ')) {
        control_headless_output(server, fd, request + (request[15] ? 16 : 15));
        return;
    }
    if (!strncmp(request, "session", 7) && (!request[7] || request[7] == ' ')) {
        control_session(server, fd, request + 7);
        return;
    }
    if (!strncmp(request, "dnd", 3) && (!request[3] || request[3] == ' ')) {
        // "dnd [on|off|toggle]": the shell's notification daemon stops or resumes its cards.
        const char *verb = request[3] ? request + 4 : "toggle";
        if (strcmp(verb, "on") && strcmp(verb, "off") && strcmp(verb, "toggle")) {
            control_reply(fd, "error: usage: dnd [on|off|toggle]\n");
            return;
        }
        char line[32];
        snprintf(line, sizeof(line), "dnd %s\n", verb);
        send_shell_line(server, line);
        control_reply(fd, "ok\n");
        return;
    }
    if (!strncmp(request, "osd", 3) && (!request[3] || request[3] == ' ')) {
        control_osd(server, fd, request[3] ? request + 4 : "");
        return;
    }
    if (!strncmp(request, "overview ", 9) || !strcmp(request, "overview")) {
        // "overview filter [TEXT]", "overview select N" and "overview view N" (from 1) drive
        // the open overview, as typing, arrows and the strip do.
        const char *verb = request + (request[8] ? 9 : 8);
        char *end = NULL;
        long number = strtol(verb + (!strncmp(verb, "select ", 7) ? 7 : !strncmp(verb, "view ", 5) ? 5 : 0), &end, 10);
        if (!server->overview.open) {
            control_reply(fd, "error: the overview is not open\n");
        } else if (!strncmp(verb, "filter", 6) && (!verb[6] || verb[6] == ' ')) {
            overview_set_filter(server, verb[6] ? verb + 7 : "");
            control_reply(fd, "ok\n");
        } else if (!strncmp(verb, "select ", 7) && end && !*end && number >= 1 &&
                   number <= server->overview.count) {
            overview_select(server, (int)number - 1);
            control_reply(fd, "ok\n");
        } else if (!strncmp(verb, "view ", 5) && end && !*end && number >= 1 &&
                   number <= server->overview.workspaces) {
            overview_view(server, (int)number - 1);
            control_reply(fd, "ok\n");
        } else {
            control_reply(fd, "error: usage: overview filter [TEXT] | select N | view N\n");
        }
        return;
    }
    // "output NAME ACTION": workspace actions switch that output instead of the focused one.
    struct wlr_output *target = NULL;
    if (!strncmp(request, "output ", 7)) {
        char name[64];
        const char *action = strchr(request + 7, ' ');
        int length = action ? (int)(action - request - 7) : 0;
        snprintf(name, sizeof(name), "%.*s", length, request + 7);
        target = action ? find_output(server, name) : NULL;
        if (!target) {
            char reply[128];
            snprintf(reply, sizeof(reply), "error: %s\n",
                     action ? "no such output" : "output needs a name and an action");
            control_reply(fd, reply);
            return;
        }
        request = action + 1;
    }
    char error[256] = "";
    int argument = 0;
    enum sh_action action = server->callbacks->command(server->callbacks->userdata, request,
                                                       &argument, error, sizeof(error));
    if (action == SH_NONE) {
        char reply[300];
        snprintf(reply, sizeof(reply), "error: %s\n", error[0] ? error : "unknown request");
        control_reply(fd, reply);
        return;
    }
    if (action == SH_SCREENSHOT) {
        // Report why no screenshot started, such as grim missing, to the caller.
        if (!take_screenshot(server, (enum sh_screenshot_mode)argument, error, sizeof(error))) {
            char reply[300];
            snprintf(reply, sizeof(reply), "error: %s\n", error);
            control_reply(fd, reply);
            return;
        }
        control_reply(fd, "ok\n");
        return;
    }
    server->target_output = target;
    run_action(server, action, argument);
    server->target_output = NULL;
    control_reply(fd, "ok\n");
}

static void control_client_close(struct sh_control_client *client) {
    if (client->subscribed)
        wl_list_remove(&client->link);
    wl_event_source_remove(client->source);
    close(client->fd);
    free(client);
}

/* The state subscribers get: "tiling on|off", "workspace N" and "focused NAME" for the focused output, and
 * "output NAME N USED TILING" for each output, with its current workspace, those holding
 * windows ("1,3", or "-"), and whether it tiles ("on" or "off"). */
/* Removes a multi-byte character cut short at the end of `text`, as snprintf leaves one. */
static void drop_partial_utf8(char *text) {
    size_t length = strlen(text), start = length;
    while (start > 0 && ((unsigned char)text[start - 1] & 0xC0) == 0x80)
        --start;
    if (start == 0)
        return;
    unsigned char lead = (unsigned char)text[start - 1];
    if (lead < 0xC0)
        return; // ASCII, or stray continuation bytes: nothing was cut
    size_t needed = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    if (length - start < needed)
        text[start - 1] = '\0';
}

static void describe_state(struct sh_server *server, char *state, size_t size) {
    struct wlr_output *focused = focused_output(server);
    size_t length = snprintf(state, size, "tiling %s\nworkspace %d\nfocused %s\n",
                             output_tiles(server, focused) ? "on" : "off",
                             focused_workspace(server), focused ? focused->name : "-");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link) {
        char used[128];
        occupied_workspaces(server, output->wlr_output, used, sizeof(used));
        if (length < size)
            length += snprintf(state + length, size - length, "output %s %d %s %s\n",
                               output->wlr_output->name,
                               *output_workspace(server, output->wlr_output->name) + 1, used,
                               output_tiles(server, output->wlr_output) ? "on" : "off");
    }
    // "urgent COUNT", then "urgent-output NAME 2,3" for each output with urgent windows, the
    // workspaces they are on, and "urgent-window OUTPUT WORKSPACE APP_ID TITLE" (tab separated
    // after the name) for each, the one that has waited longest first.
    unsigned count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) count += toplevel->urgent;
    if (length < size)
        length += snprintf(state + length, size - length, "urgent %u\n", count);
    wl_list_for_each_reverse(output, &server->outputs, link) {
        unsigned used = 0;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->workspace < 32 &&
                !strcmp(toplevel->output, output->wlr_output->name))
                used |= 1u << toplevel->workspace;
        }
        if (!used || length >= size)
            continue;
        length += snprintf(state + length, size - length, "urgent-output %s", output->wlr_output->name);
        for (int i = 0, first = 1; i < 32 && length < size; ++i) {
            if (used & 1u << i) {
                length += snprintf(state + length, size - length, "%s%d", first ? " " : ",", i + 1);
                first = 0;
            }
        }
        if (length < size)
            length += snprintf(state + length, size - length, "\n");
    }
    unsigned last = 0;
    for (unsigned listed = 0; listed < count && listed < 16 && length < size; ++listed) {
        struct sh_toplevel *next = NULL;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->urgent_order > last &&
                (!next || toplevel->urgent_order < next->urgent_order))
                next = toplevel;
        }
        if (!next)
            break;
        last = next->urgent_order;
        // The title as the taskbar has it (the shell finds the window by it), cut short at a
        // character boundary.
        char app_id[64], title[256];
        const char *raw_app_id = toplevel_app_id(next), *raw_title = toplevel_title(next);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "Untitled");
        drop_partial_utf8(title);
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        length += snprintf(state + length, size - length, "urgent-window %s\t%d\t%s\t%s\n",
                           next->output, next->workspace + 1, app_id, title);
    }
}

/* Subscribers get the state after each change, and "launcher OUTPUT" or "palette OUTPUT" when a binding
 * asks the shell for its application menu or command palette; a subscriber that cannot keep up is dropped rather than
 * blocking the compositor. */
static bool control_send_state(struct sh_control_client *client, const char *state) {
    size_t length = strlen(state);
    return send(client->fd, state, length, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)length;
}

static void notify_subscribers(struct sh_server *server) {
    overview_touch(server, true); // a change of windows or workspaces, when it is open
    char state[sizeof(server->sent_state)];
    describe_state(server, state, sizeof(state));
    if (!strcmp(state, server->sent_state))
        return;
    strcpy(server->sent_state, state);
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        if (!control_send_state(client, state))
            control_client_close(client);
    }
}

/* Sends every subscriber an event, such as a request for the shell. */
static void send_event(struct sh_server *server, const char *text, size_t length) {
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        if (send(client->fd, text, length, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)length)
            control_client_close(client);
    }
}

/* Asks the shell to open something (`what`: "launcher" or "palette") on the output under the
 * pointer. */
static void request_shell(struct sh_server *server, const char *what) {
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (!output)
        return;
    char line[128];
    int length = snprintf(line, sizeof(line), "%s %s\n", what, output->name);
    if (length < 0 || (size_t)length >= sizeof(line))
        return;
    send_event(server, line, (size_t)length);
}

static void send_shell_line(struct sh_server *server, const char *line) {
    send_event(server, line, strlen(line));
}

static void request_launcher(struct sh_server *server) {
    request_shell(server, "launcher");
}

static void request_palette(struct sh_server *server) {
    request_shell(server, "palette");
}

static int control_client_readable(int fd, uint32_t mask, void *data) {
    struct sh_control_client *client = data;
    if (client->subscribed) {
        char ignored[64];
        ssize_t count = read(fd, ignored, sizeof(ignored));
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR))
            control_client_close(client);
        return 0;
    }
    ssize_t count =
        read(fd, client->request + client->length, sizeof(client->request) - 1 - client->length);
    if (count < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    if (count <= 0) {
        control_client_close(client);
        return 0;
    }
    client->length += (size_t)count;
    client->request[client->length] = '\0';
    char *newline = strchr(client->request, '\n');
    if (!newline && client->length < sizeof(client->request) - 1)
        return 0;
    if (newline)
        *newline = '\0';
    if (newline && !strcmp(client->request, "subscribe")) {
        client->subscribed = true;
        wl_list_insert(&client->server->subscribers, &client->link);
        char state[sizeof(client->server->sent_state)];
        describe_state(client->server, state, sizeof(state));
        if (send(fd, "ok\n", 3, MSG_NOSIGNAL | MSG_DONTWAIT) != 3 ||
            !control_send_state(client, state))
            control_client_close(client);
        return 0;
    }
    // Replies are small; a blocking write keeps the protocol simple.
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    if (newline)
        control_handle(client->server, fd, client->request);
    else
        control_reply(fd, "error: request too long\n");
    control_client_close(client);
    return 0;
}

static int control_accept(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    int client_fd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client_fd < 0)
        return 0;
    struct sh_control_client *client = calloc(1, sizeof(*client));
    if (!client) {
        close(client_fd);
        return 0;
    }
    client->server = server;
    client->fd = client_fd;
    client->source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), client_fd,
                                          WL_EVENT_READABLE, control_client_readable, client);
    if (!client->source) {
        close(client_fd);
        free(client);
    }
    return 0;
}

static void open_control_socket(struct sh_server *server, const char *wayland_socket) {
    server->control_fd = -1;
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (!runtime || !*runtime ||
        snprintf(server->control_path, sizeof(server->control_path), "%s/shaodesk.%s.sock", runtime,
                 wayland_socket) >= (int)sizeof(server->control_path) ||
        strlen(server->control_path) >= sizeof(address.sun_path)) {
        wlr_log(WLR_ERROR, "No usable XDG_RUNTIME_DIR; control socket disabled");
        server->control_path[0] = '\0';
        return;
    }
    strcpy(address.sun_path, server->control_path);
    unlink(server->control_path); // A stale socket from a crashed session.
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(fd, SOMAXCONN) < 0) {
        wlr_log_errno(WLR_ERROR, "Cannot create control socket %s", server->control_path);
        if (fd >= 0)
            close(fd);
        server->control_path[0] = '\0';
        return;
    }
    server->control_fd = fd;
    server->control_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), fd,
                                                  WL_EVENT_READABLE, control_accept, server);
    setenv("SHAODESK_SOCKET", server->control_path, true);
    wlr_log(WLR_INFO, "Control socket: %s", server->control_path);
}

static void close_control_socket(struct sh_server *server) {
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        control_client_close(client);
    }
    if (server->control_source)
        wl_event_source_remove(server->control_source);
    if (server->control_fd >= 0)
        close(server->control_fd);
    if (server->control_path[0])
        unlink(server->control_path);
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
