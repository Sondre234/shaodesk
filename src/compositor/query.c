/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The control socket's queries (`shaodesk msg get ...`): what the windows, outputs, workspaces
 * and effects are doing, for scripts, the shell and the tests. */
#include "server.h"

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

static void get_outputs(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link)
        control_describe_output(server, fd, output);
    wl_list_for_each_reverse(output, &server->disabled_outputs, link)
        control_describe_output(server, fd, output);
}

static void get_workspace(struct sh_server *server, int fd, const char *arguments) {
    char reply[32];
    snprintf(reply, sizeof(reply), "ok\n%d\n", focused_workspace(server));
    control_reply(fd, reply);
}

static void get_workspaces(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_layout(struct sh_server *server, int fd, const char *arguments) {
    // Layout, master ratio and master count of the focused output's current workspace, or
    // of "get layout OUTPUT [WORKSPACE]" (from 1).
    static const char *const names[] = {"dwindle", "master", "spiral", "monocle", "scroll"};
    struct wlr_output *output = focused_output(server);
    int workspace = output ? *output_workspace(server, output->name) : 0;
    if (arguments) {
        char name[64];
        int number = 0, fields = sscanf(arguments, "%63s %d", name, &number);
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
}

static void get_tiling(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, output_tiles(server, focused_output(server)) ? "ok\non\n" : "ok\noff\n");
}

static void get_urgent(struct sh_server *server, int fd, const char *arguments) {
    control_describe_urgent(server, fd);
}

static void get_windows(struct sh_server *server, int fd, const char *arguments) {
    control_describe_windows(server, fd);
}

static void get_pid_at(struct sh_server *server, int fd, const char *arguments) {
    // The process of the window drawn at layout point X Y, or no line when there is none.
    double x, y;
    if (sscanf(arguments, "%lf %lf", &x, &y) != 2) {
        control_reply(fd, "error: usage: get pid_at X Y\n");
        return;
    }
    struct sh_toplevel *toplevel = toplevel_at(server, x, y);
    pid_t pid = toplevel ? toplevel_pid(toplevel) : 0;
    char reply[32] = "ok\n";
    if (pid > 0)
        snprintf(reply, sizeof(reply), "ok\n%d\n", (int)pid);
    control_reply(fd, reply);
}

static void get_swallow(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_guides(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_animations(struct sh_server *server, int fd, const char *arguments) {
    // Running animations, and the scene trees stacked for windows (with closing copies).
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%zu\t%d\t%zu\n", sh_animator_running(server->animator),
             wl_list_length(&server->windows->children) +
                 wl_list_length(&server->fullscreen->children) +
                 wl_list_length(&server->fullscreen_cover->children),
             sh_animator_tweens(server->animator));
    control_reply(fd, reply);
}

static void get_stats(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_frame_times(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_dim(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_peek(struct sh_server *server, int fd, const char *arguments) {
    // How far windows have faded toward the desktop (thousandths), whether peeking, and
    // whether a held key keeps it up.
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%ld\t%d\t%d\n",
             lround(1000 * sh_fade_value(&server->peek_fade, now_ms())), server->peeking,
             server->peek_keycode != 0);
    control_reply(fd, reply);
}

static void get_opacities(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_zoom(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_night_light(struct sh_server *server, int fd, const char *arguments) {
    // The temperature applied now, the override (0 schedule, 1 forced neutral, 2 forced
    // warm), and whether the schedule is enabled.
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%d\t%d\t%d\n", server->night_kelvin,
             server->night_mode, server_settings(server)->effects.night_light);
    control_reply(fd, reply);
}

static void get_overview(struct sh_server *server, int fd, const char *arguments) {
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
}

static void get_layers(struct sh_server *server, int fd, const char *arguments) {
    control_describe_layers(server, fd);
}

/* The queries, "get NAME" (or "get NAME ARGUMENTS" for one that takes them), which answer
 * even while the session is locked. A handler gets NULL for no arguments. */
static const struct {
    const char *name;
    void (*run)(struct sh_server *server, int fd, const char *arguments);
    bool arguments;
} queries[] = {
    {"outputs", get_outputs, false},
    {"workspace", get_workspace, false},
    {"workspaces", get_workspaces, false},
    {"layout", get_layout, true},
    {"tiling", get_tiling, false},
    {"urgent", get_urgent, false},
    {"windows", get_windows, false},
    {"pid_at", get_pid_at, true},
    {"swallow", get_swallow, false},
    {"guides", get_guides, false},
    {"animations", get_animations, false},
    {"stats", get_stats, false},
    {"frame_times", get_frame_times, false},
    {"dim", get_dim, false},
    {"peek", get_peek, false},
    {"opacities", get_opacities, false},
    {"zoom", get_zoom, false},
    {"night_light", get_night_light, false},
    {"overview", get_overview, false},
    {"layers", get_layers, false},
};

/* Answers `request` if it is a query; false if it is not one. */
bool run_query(struct sh_server *server, int fd, const char *request) {
    if (strncmp(request, "get ", 4))
        return false;
    const char *name = request + 4;
    for (size_t i = 0; i < sizeof(queries) / sizeof(*queries); ++i) {
        size_t length = strlen(queries[i].name);
        if (strncmp(name, queries[i].name, length))
            continue;
        if (!name[length] || (queries[i].arguments && name[length] == ' ')) {
            queries[i].run(server, fd, name[length] ? name + length + 1 : NULL);
            return true;
        }
    }
    return false;
}
