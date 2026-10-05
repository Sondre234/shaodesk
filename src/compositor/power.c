/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power: suspend, hibernate, reboot and power off through logind, and what it allows of them,
 * over a connection that never blocks the compositor (src/login1.c); locking the screen with
 * power.lock_command, before suspend and hibernate too; and logging out. */
#include "server.h"

#include <ctype.h>
#include <stdarg.h>

/* How long a locker gets to lock the screen before a suspend gives up. */
#define LOCK_TIMEOUT_MS 5000

/* Each power action, in the order menus list them: its name in the action table and in
 * messages, and the logind method that carries it out (SH_LOGIN1_METHODS for none). */
static const struct {
    enum sh_action action;
    const char *name, *label;
    enum sh_login1_method method;
} actions[] = {
    {SH_LOCK, "lock", "lock the screen", SH_LOGIN1_METHODS},
    {SH_SUSPEND, "suspend", "suspend", SH_LOGIN1_SUSPEND},
    {SH_HIBERNATE, "hibernate", "hibernate", SH_LOGIN1_HIBERNATE},
    {SH_REBOOT, "reboot", "reboot", SH_LOGIN1_REBOOT},
    {SH_POWER_OFF, "poweroff", "power off", SH_LOGIN1_POWER_OFF},
    {SH_LOGOUT, "logout", "log out", SH_LOGIN1_METHODS},
};

static int action_index(enum sh_action action) {
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); ++i)
        if (actions[i].action == action)
            return (int)i;
    return -1;
}

bool power_action(enum sh_action action) {
    return action_index(action) >= 0;
}

const char *power_action_name(enum sh_action action) {
    int index = action_index(action);
    return index >= 0 ? actions[index].name : "";
}

static const char *action_label(enum sh_action action) {
    int index = action_index(action);
    return index >= 0 ? actions[index].label : "";
}

/* Tells the user what went wrong with a power action they no longer wait on: in the log, and
 * across the panel. */
static void power_report(struct sh_server *server, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
static void power_report(struct sh_server *server, const char *format, ...) {
    char text[320];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    text[0] = (char)toupper((unsigned char)text[0]);
    for (char *c = text; *c; ++c)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
    wlr_log(WLR_ERROR, "%s", text);
    char line[352];
    snprintf(line, sizeof(line), "power-error %s\n", text);
    send_shell_line(server, line);
}

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
    if (power->step == SH_POWER_CALLING) {
        power->step = SH_POWER_IDLE;
        power_report(server, "%s: lost the connection to logind", action_label(power->action));
    }
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

static void done(void *data, enum sh_login1_method method, const char *error) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    if (power->step != SH_POWER_CALLING)
        return;
    power->step = SH_POWER_IDLE;
    if (error)
        power_report(server, "%s failed: %s", action_label(power->action), error);
    else
        wlr_log(WLR_INFO, "logind accepted %s", sh_login1_method_name(method));
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
                 "a headless session uses only the logind on the bus SHAODESK_LOGIN1_BUS names");
        return false;
    }
    const struct sh_login1_handler handler = {.data = server, .answer = answered, .done = done};
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

/* Carries out the action under way: hands it to logind, or ends the session. */
static bool power_call(struct sh_server *server, char *error, size_t error_size) {
    struct sh_power *power = &server->power;
    enum sh_login1_method method = actions[action_index(power->action)].method;
    if (power->action == SH_LOGOUT) {
        wlr_log(WLR_INFO, "Logging out");
        power->step = SH_POWER_IDLE;
        wl_display_terminate(server->wl_display);
        return true;
    }
    if (!power->login1 || !sh_login1_call(power->login1, method)) {
        snprintf(error, error_size, "cannot reach logind to %s", action_label(power->action));
        power->step = SH_POWER_IDLE;
        return false;
    }
    wlr_log(WLR_INFO, "Asking logind to %s", action_label(power->action));
    power->step = SH_POWER_CALLING;
    power_watch(server);
    return true;
}

/* Whether power.lock_command can lock the screen; with `start`, starts it too. */
static bool locker(struct sh_server *server, bool start, char *error, size_t error_size) {
    const struct sh_callbacks *callbacks = server->callbacks;
    if (!callbacks->lock) {
        snprintf(error, error_size, "no screen locker");
        return false;
    }
    return callbacks->lock(callbacks->userdata, start, error, error_size);
}

/* Starts the screen locker, unless the screen is locked already. */
static bool power_lock(struct sh_server *server, char *error, size_t error_size) {
    if (server->locked)
        return true;
    if (!locker(server, true, error, error_size))
        return false;
    wlr_log(WLR_INFO, "Started the screen locker");
    return true;
}

/* Whether the screen has to lock before the machine sleeps: power.lock_before_sleep with a
 * locker to lock it, and no lock holding yet. */
static bool lock_first(struct sh_server *server) {
    char ignored[256];
    return server_settings(server)->lock_before_sleep &&
           locker(server, false, ignored, sizeof(ignored)) &&
           !(server->lock && server->lock->locked_sent);
}

/* The step under way ran out of time. */
static int power_timeout(void *data) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    if (power->step == SH_POWER_LOCKING) {
        power->step = SH_POWER_IDLE;
        power_report(server, "%s cancelled: the screen did not lock within %d seconds",
                     action_label(power->action), LOCK_TIMEOUT_MS / 1000);
    }
    return 0;
}

/* The lock holds on every output. */
void power_locked(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (power->step != SH_POWER_LOCKING)
        return;
    wl_event_source_timer_update(power->timer, 0);
    char error[300];
    if (!power_call(server, error, sizeof(error)))
        power_report(server, "%s", error);
}

bool power_start(struct sh_server *server, enum sh_action action, char *error,
                 size_t error_size) {
    struct sh_power *power = &server->power;
    int index = action_index(action);
    if (index < 0) {
        snprintf(error, error_size, "not a power action");
        return false;
    }
    if (action == SH_LOCK)
        return power_lock(server, error, error_size);
    if (power->step != SH_POWER_IDLE) {
        snprintf(error, error_size, "%s is already under way", action_label(power->action));
        return false;
    }
    enum sh_login1_method method = actions[index].method;
    char reason[256];
    if (method != SH_LOGIN1_METHODS && !power_connect(server, reason, sizeof(reason))) {
        snprintf(error, error_size, "logind is out of reach: %s", reason);
        return false;
    }
    if (method != SH_LOGIN1_METHODS) {
        // A refusal stands on the last answer; asking again keeps the next one current.
        const char *answer = power->answers[method];
        bool refused = !strcmp(answer, "no") || !strcmp(answer, "na");
        if (refused)
            snprintf(error, error_size, "logind does not allow %s here (Can%s: %s)",
                     actions[index].label, sh_login1_method_name(method), answer);
        power_ask(server);
        if (refused)
            return false;
    }
    power->action = action;
    if ((action == SH_SUSPEND || action == SH_HIBERNATE) && lock_first(server)) {
        if (!power_lock(server, error, error_size))
            return false;
        power->step = SH_POWER_LOCKING;
        wl_event_source_timer_update(power->timer, LOCK_TIMEOUT_MS);
        return true;
    }
    return power_call(server, error, error_size);
}

void power_run(struct sh_server *server, enum sh_action action) {
    char error[300];
    if (!power_start(server, action, error, sizeof(error)))
        power_report(server, "%s", error);
}

/* The `index`th power action, in menu order, and whether it may run: "yes" or "no" for those
 * of shaodesk's own; for logind's, its answer, "unknown" until it gives one, or "unavailable"
 * without logind. False past the last. */
bool power_describe(struct sh_server *server, size_t index, const char **name,
                    const char **status) {
    if (index >= sizeof(actions) / sizeof(*actions))
        return false;
    struct sh_power *power = &server->power;
    enum sh_login1_method method = actions[index].method;
    char ignored[256];
    *name = actions[index].name;
    *status = actions[index].action == SH_LOCK ? (locker(server, false, ignored, sizeof(ignored))
                                                      ? "yes"
                                                      : "no")
              : method == SH_LOGIN1_METHODS ? "yes"
              : !power->login1             ? "unavailable"
              : power->answers[method][0]   ? power->answers[method]
                                            : "unknown";
    return true;
}

/* What `get power` says of the action under way: its name and step, or "-". */
const char *power_pending(struct sh_server *server, char *text, size_t size) {
    static const char *const steps[] = {"idle", "locking", "calling"};
    struct sh_power *power = &server->power;
    if (power->step == SH_POWER_IDLE)
        return "-";
    snprintf(text, size, "%s %s", power_action_name(power->action), steps[power->step]);
    return text;
}

void power_reload(struct sh_server *server) {
    power_ask(server);
}

void power_init(struct sh_server *server) {
    server->power.timer =
        wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display), power_timeout,
                                server);
    char error[256];
    if (!power_connect(server, error, sizeof(error)))
        wlr_log(WLR_INFO, "Suspend, hibernate, reboot and power off are unavailable: %s", error);
}

void power_finish(struct sh_server *server) {
    server->power.step = SH_POWER_IDLE;
    if (server->power.timer)
        wl_event_source_remove(server->power.timer);
    server->power.timer = NULL;
    power_disconnect(server);
}
