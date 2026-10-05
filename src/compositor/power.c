/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power: what logind allows of suspend, hibernate, reboot and power off, through a connection
 * that never blocks the compositor (src/login1.c). */
#include "server.h"

/* Watches the connection for what sd-bus waits for next. */
static void power_watch(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    wl_event_source_fd_update(power->bus, WL_EVENT_READABLE | (sh_login1_wants_write(power->login1)
                                                                    ? WL_EVENT_WRITABLE
                                                                    : 0));
    int timeout = sh_login1_timeout(power->login1);
    wl_event_source_timer_update(power->bus_timer, timeout < 0 ? 0 : timeout > 0 ? timeout : 1);
}

static void power_disconnect(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    wl_event_source_remove(power->bus);
    wl_event_source_remove(power->bus_timer);
    power->bus = power->bus_timer = NULL;
    sh_login1_destroy(power->login1);
    power->login1 = NULL;
    memset(power->answers, 0, sizeof(power->answers));
}

static int bus_ready(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    if (!sh_login1_dispatch(server->power.login1)) {
        wlr_log(WLR_ERROR, "Lost the connection to logind");
        power_disconnect(server);
        notify_subscribers(server);
        return 0;
    }
    power_watch(server);
    return 0;
}

static int bus_timeout(void *data) {
    return bus_ready(-1, 0, data);
}

/* Asks logind afresh which actions it allows; the answers come to `answered`. */
static void power_ask(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i)
        sh_login1_ask(power->login1, (enum sh_login1_method)i);
    power_watch(server);
}

static void answered(void *data, enum sh_login1_method method, const char *answer,
                     const char *error) {
    struct sh_server *server = data;
    if (!answer)
        wlr_log(WLR_ERROR, "logind did not say whether it allows %s: %s",
                sh_login1_method_name(method), error);
    snprintf(server->power.answers[method], sizeof(server->power.answers[method]), "%s",
             answer ? answer : "");
    notify_subscribers(server);
}

/* Connects to logind unless already connected: on the system bus, or on the bus
 * $SHAODESK_LOGIN1_BUS names. A headless compositor (the tests) uses only the latter, so
 * nothing it is asked to do can reach the machine's own logind. */
static bool power_connect(struct sh_server *server, char *error, size_t error_size) {
    struct sh_power *power = &server->power;
    if (power->login1)
        return true;
    const char *address = getenv("SHAODESK_LOGIN1_BUS");
    if (address && !*address)
        address = NULL;
    if (!address && !power->system_bus) {
        snprintf(error, error_size,
                 "a headless session reaches logind only through SHAODESK_LOGIN1_BUS");
        return false;
    }
    const struct sh_login1_handler handler = {.data = server, .answer = answered};
    power->login1 = sh_login1_connect(address, &handler, error, error_size);
    if (!power->login1)
        return false;
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    power->bus = wl_event_loop_add_fd(loop, sh_login1_fd(power->login1), WL_EVENT_READABLE,
                                      bus_ready, server);
    power->bus_timer = wl_event_loop_add_timer(loop, bus_timeout, server);
    if (!power->bus || !power->bus_timer) {
        snprintf(error, error_size, "cannot watch the connection to logind");
        if (power->bus)
            wl_event_source_remove(power->bus);
        if (power->bus_timer)
            wl_event_source_remove(power->bus_timer);
        sh_login1_destroy(power->login1);
        power->login1 = NULL;
        return false;
    }
    power_ask(server);
    return true;
}

void power_init(struct sh_server *server) {
    char error[256];
    if (!power_connect(server, error, sizeof(error)))
        wlr_log(WLR_INFO, "Suspend, hibernate, reboot and power off are unavailable: %s", error);
}

void power_finish(struct sh_server *server) {
    power_disconnect(server);
}
