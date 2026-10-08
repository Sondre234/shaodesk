/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The display settings window's monitors: the settings it kept, read from
 * $XDG_STATE_HOME/shaodesk/outputs (src/output_state.c) as the configuration loads and laid over
 * outputs.monitors, each for the monitor it was kept for. */
#include "server.h"
#include "shaodesk/lid.h"

/* Reads the kept settings again: at startup, before any monitor is configured, and as the
 * configuration reloads. A missing file is none; one that cannot be read is said and passed
 * over. */
void display_settings_load(struct sh_server *server) {
    struct sh_output_state *saved = &server->display_settings.saved;
    memset(saved, 0, sizeof(*saved));
    char path[PATH_MAX], error[128];
    if (!sh_output_state_path(path, sizeof(path)))
        return;
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT)
            wlr_log_errno(WLR_ERROR, "Cannot read %s", path);
        return;
    }
    if (!sh_output_state_read(saved, file, error, sizeof(error)))
        wlr_log(WLR_ERROR, "Passing over %s: %s", path, error);
    else if (saved->count > 0)
        wlr_log(WLR_INFO, "Monitors kept from the display settings window: %d, in %s",
                saved->count, path);
    fclose(file);
}

/* What the display settings window kept for the monitor on `output`'s connector, if it is the
 * monitor it was kept for; NULL otherwise. */
const struct sh_output_saved *saved_output(const struct sh_output *output) {
    char description[256];
    output_description(output->wlr_output, description, sizeof(description));
    return sh_output_state_find(&output->server->display_settings.saved, output->wlr_output->name,
                                description);
}

/* The primary monitor's name: the connected one the window made primary, else outputs.primary
 * (a connector name, or "desc:" and the start of a description); "" for none. */
const char *primary_output_name(struct sh_server *server) {
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) {
            const struct sh_output_saved *saved = saved_output(output);
            if (saved && saved->primary)
                return output->wlr_output->name;
        }
    }
    return server_settings(server)->primary_output;
}

/* Whether a monitor's settings, the configuration's or those the window kept, ask for HDR. */
bool hdr_asked(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < settings->monitor_count; ++i) {
        if (settings->monitors[i].hdr)
            return true;
    }
    const struct sh_output_state *saved = &server->display_settings.saved;
    for (int i = 0; i < saved->count; ++i) {
        if (saved->outputs[i].monitor.hdr)
            return true;
    }
    return false;
}

/* Tabs and line breaks in a monitor's description would break the columns. */
static void plain_text(char *text) {
    for (char *c = text; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
}

/* A mode as outputs.monitors writes it, its refresh in Hz to the thousandth: "2560x1440@143.912",
 * or without one "2560x1440". */
static void format_mode(char *text, size_t size, int width, int height, int refresh) {
    if (refresh > 0)
        snprintf(text, size, "%dx%d@%d.%03d", width, height, refresh / 1000, refresh % 1000);
    else
        snprintf(text, size, "%dx%d", width, height);
}

/* One line of `get monitors`: what the window shows of a monitor and offers it. Where the monitor
 * shows a picture (in the layout, or mirroring) its mode, scale, transform, adaptive sync and
 * place are what it has now; elsewhere what its settings say. */
static void describe_monitor(struct sh_server *server, int fd, struct sh_output *output) {
    struct wlr_output *o = output->wlr_output;
    const struct sh_monitor *monitor = output_monitor(server_settings(server), output);
    const char *source = output->has_override ? "override"
                         : saved_output(output) ? "window"
                         : monitor              ? "config"
                                                : "default";
    bool live = !output->disabled || output->mirror;
    int width = o->width, height = o->height, refresh = o->refresh;
    struct wlr_output_mode *preferred = wlr_output_preferred_mode(o);
    if (!live && monitor && monitor->width > 0) {
        width = monitor->width, height = monitor->height, refresh = monitor->refresh;
    } else if (!live && preferred) {
        width = preferred->width, height = preferred->height, refresh = preferred->refresh;
    }
    float scale = live ? o->scale : monitor && monitor->scale > 0 ? monitor->scale : 1;
    int transform = live ? (int)o->transform : monitor ? monitor->transform : 0;
    // A mirror stands where the monitor it mirrors does, as wlr-output-management says.
    struct sh_output *source_output = mirrored_output(output);
    struct sh_output *placed = source_output ? source_output : output;
    struct wlr_box box = {0};
    if (!placed->disabled)
        wlr_output_layout_get_box(server->output_layout, placed->wlr_output, &box);
    else if (monitor && monitor->positioned)
        box.x = monitor->x, box.y = monitor->y;
    const char *vrr = !o->adaptive_sync_supported ? "-"
                      : live ? (o->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED ? "on"
                                                                                           : "off")
                      : monitor && monitor->vrr ? "on"
                                                : "off";
    bool hdr = monitor && monitor->hdr;
    const char *why = hdr_unavailable(output);
    if (!why && hdr && !output_is_hdr(output) && output->hdr_refused && *output->hdr_refused)
        why = output->hdr_refused;
    const char *primary = primary_output_name(server);
    char description[256], mode[48], line[1024];
    output_description(o, description, sizeof(description));
    plain_text(description);
    format_mode(mode, sizeof(mode), width, height, refresh);
    // Whether it shows a picture: "off" while disabled or turned off, "lid" while the lid holds
    // it off.
    const char *state = lid_holds_off(server, output) ? "lid" : live && o->enabled ? "on" : "off";
    snprintf(line, sizeof(line),
             "%s\t%s\t%d\t%s\t%d\t%s\t%s\t%d\t%d\t%s\t%.9g\t%d\t%s\t%d\t%d\t%s\t%s\t%s\t%d\t",
             o->name, description, sh_output_built_in(o->name), source,
             !monitor || monitor->enabled, state,
             source_output ? source_output->wlr_output->name : "-", box.x, box.y, mode, scale,
             transform, vrr, monitor && monitor->bit_depth == 10 ? 10 : 8,
             live && deep_format(o->render_format) ? 10 : 8, hdr ? "on" : "off",
             output_is_hdr(output) ? "hdr" : "sdr", why ? why : "-",
             primary[0] && output_key_matches(primary, o));
    control_reply(fd, line);
    // The modes it offers, each once, its preferred one marked; one without any (a nested or
    // headless output) offers the one it has.
    struct wlr_output_mode *each, *before;
    int listed = 0;
    wl_list_for_each(each, &o->modes, link) {
        bool again = false;
        wl_list_for_each(before, &o->modes, link) {
            if (before == each)
                break;
            again |= before->width == each->width && before->height == each->height &&
                     before->refresh == each->refresh;
        }
        if (again)
            continue;
        format_mode(mode, sizeof(mode), each->width, each->height, each->refresh);
        snprintf(line, sizeof(line), "%s%s%s", listed++ ? "," : "", mode, each->preferred ? "*" : "");
        control_reply(fd, line);
    }
    if (!listed) {
        format_mode(mode, sizeof(mode), width, height, refresh);
        control_reply(fd, mode);
    }
    control_reply(fd, "\n");
}

/* `get monitors`: a line per monitor connected, in the order of `get outputs`. */
void describe_monitors(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link) describe_monitor(server, fd, output);
    wl_list_for_each_reverse(output, &server->disabled_outputs, link)
        describe_monitor(server, fd, output);
}
