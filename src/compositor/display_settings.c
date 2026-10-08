/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The display settings window's monitors: the settings it kept, read from
 * $XDG_STATE_HOME/shaodesk/outputs (src/output_state.c) as the configuration loads and laid over
 * outputs.monitors, each for the monitor it was kept for. */
#include "server.h"

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
