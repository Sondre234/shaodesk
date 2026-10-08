/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Turning monitors off and on without taking them out of the layout, for wlr-output-power-
 * management clients (wlopm, idle daemons). A monitor that is off keeps its windows, workspaces
 * and panels; its wlr_output is disabled, so it neither scans out nor draws frames. */
#include "server.h"

/* Turns a monitor in the layout off or on. On, it is configured as outputs.monitors says, which
 * a reload while it was off may have changed. False when it is not in the layout or the
 * backend refuses. */
bool set_output_power(struct sh_output *output, bool on) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    if (output->disabled)
        return false;
    if (on != output->powered_off)
        return true;
    if (!on) {
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, false);
        bool done = wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);
        if (!done) {
            wlr_log(WLR_ERROR, "Cannot turn %s off", wlr_output->name);
            return false;
        }
        output->powered_off = true;
        server->displays_off_at = now_ms();
        wlr_log(WLR_INFO, "Turned %s off", wlr_output->name);
        // Nothing shows on it, so a lock waits for it no longer.
        send_locked_if_presented(server);
        return true;
    }
    output->powered_off = false;
    configure_output(server, output);
    if (!wlr_output->enabled) {
        output->powered_off = true;
        wlr_log(WLR_ERROR, "Cannot turn %s on", wlr_output->name);
        return false;
    }
    arrange_outputs(server); // in case a reload changed its mode or scale meanwhile
    wlr_output_schedule_frame(wlr_output);
    wlr_log(WLR_INFO, "Turned %s on", wlr_output->name);
    return true;
}

/* A wlr-output-power-management client sets a monitor's mode. A monitor out of the layout
 * (outputs.monitors' enabled = false) stays as the configuration has it. */
void output_power_set_mode(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_power_set_mode);
    const struct wlr_output_power_v1_set_mode_event *event = data;
    struct sh_output *output = sh_output_for(server, event->output);
    if (output)
        set_output_power(output, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
}
