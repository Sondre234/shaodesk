/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The control socket (`shaodesk msg`): one request per connection, answered with "ok" and any
 * output, or with "error: ...". Subscribers stay connected and get the state after each change,
 * and events for the shell. */
#include "server.h"

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

static int session_name_compare(const struct dirent **a, const struct dirent **b);
static int session_name_filter(const struct dirent *entry);

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
static bool run_query(struct sh_server *server, int fd, const char *request) {
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

static void control_handle(struct sh_server *server, int fd, const char *request) {
    if (run_query(server, fd, request))
        return;
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

/* The state subscribers get: "tiling on|off", "workspace N" and "focused NAME" for the focused
 * output, and "output NAME N USED TILING" for each output, with its current workspace, those
 * holding windows ("1,3", or "-"), and whether it tiles ("on" or "off"). */
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

void notify_subscribers(struct sh_server *server) {
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
void send_event(struct sh_server *server, const char *text, size_t length) {
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        if (send(client->fd, text, length, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)length)
            control_client_close(client);
    }
}

/* Asks the shell to open something (`what`: "launcher" or "palette") on the output under the
 * pointer. */
void request_shell(struct sh_server *server, const char *what) {
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

void send_shell_line(struct sh_server *server, const char *line) {
    send_event(server, line, strlen(line));
}

void request_launcher(struct sh_server *server) {
    request_shell(server, "launcher");
}

void request_palette(struct sh_server *server) {
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

void open_control_socket(struct sh_server *server, const char *wayland_socket) {
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

void close_control_socket(struct sh_server *server) {
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

static int session_name_compare(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}
static int session_name_filter(const struct dirent *entry) {
    return sh_session_valid_name(entry->d_name);
}
