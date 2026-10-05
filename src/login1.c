// SPDX-License-Identifier: GPL-3.0-or-later
#include "shaodesk/login1.h"

#include <stdio.h>

static const char *const method_names[SH_LOGIN1_METHODS] = {"PowerOff", "Reboot", "Suspend",
                                                            "Hibernate"};

const char *sh_login1_method_name(enum sh_login1_method method) {
    return (unsigned)method < SH_LOGIN1_METHODS ? method_names[method] : "?";
}

#if SHAODESK_SDBUS_SYSTEMD
#include <systemd/sd-bus.h>
#elif SHAODESK_SDBUS_ELOGIND
#include <elogind/sd-bus.h>
#elif SHAODESK_SDBUS_BASU
#include <basu/sd-bus.h>
#endif

#if SHAODESK_SDBUS_SYSTEMD || SHAODESK_SDBUS_ELOGIND || SHAODESK_SDBUS_BASU
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOGIN1 "org.freedesktop.login1"
#define LOGIN1_PATH "/org/freedesktop/login1"
#define LOGIN1_MANAGER "org.freedesktop.login1.Manager"
/* A power action may wait for a password; logind answers once it has been given. */
#define CALL_TIMEOUT_USEC (10ULL * 60 * 1000 * 1000)

/* One call in flight, and what its reply is for. */
struct pending {
    struct sh_login1 *login1;
    enum sh_login1_method method;
    sd_bus_slot *slot;
};

struct sh_login1 {
    sd_bus *bus;
    struct sh_login1_handler handler;
    struct pending asks[SH_LOGIN1_METHODS], calls[SH_LOGIN1_METHODS], inhibit;
    sd_bus_slot *sleep_match;
};

/* The reason a reply carries, or NULL when it is not an error. */
static const char *reply_error(sd_bus_message *reply) {
    const sd_bus_error *error = sd_bus_message_get_error(reply);
    if (!error)
        return NULL;
    return error->message ? error->message : error->name ? error->name : "unknown error";
}

static int asked(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    const char *error = reply_error(reply), *answer = NULL;
    int r = error ? 0 : sd_bus_message_read(reply, "s", &answer);
    if (r < 0)
        error = strerror(-r);
    if (login1->handler.answer)
        login1->handler.answer(login1->handler.data, pending->method, error ? NULL : answer,
                               error);
    return 0;
}

static int called(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    if (login1->handler.done)
        login1->handler.done(login1->handler.data, pending->method, reply_error(reply));
    return 0;
}

static int inhibited(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    const char *error = reply_error(reply);
    int held = -1, fd = -1;
    int r = error ? 0 : sd_bus_message_read(reply, "h", &held);
    if (r < 0)
        error = strerror(-r);
    // The reply owns `held`; keep a copy of our own, clear of the standard descriptors.
    else if (!error && (fd = fcntl(held, F_DUPFD_CLOEXEC, 3)) < 0)
        error = strerror(errno);
    if (login1->handler.inhibited)
        login1->handler.inhibited(login1->handler.data, fd, error);
    else if (fd >= 0)
        close(fd);
    return 0;
}

static int prepare_for_sleep(sd_bus_message *message, void *data, sd_bus_error *unused) {
    struct sh_login1 *login1 = data;
    int before = 0;
    if (sd_bus_message_read(message, "b", &before) >= 0 && login1->handler.sleep)
        login1->handler.sleep(login1->handler.data, before);
    return 0;
}

struct sh_login1 *sh_login1_connect(const char *address, const struct sh_login1_handler *handler,
                                    char *error, size_t error_size) {
    struct sh_login1 *login1 = calloc(1, sizeof(*login1));
    if (!login1) {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    login1->handler = *handler;
    int r;
    if (address) {
        r = sd_bus_new(&login1->bus);
        if (r >= 0)
            r = sd_bus_set_address(login1->bus, address);
        if (r >= 0)
            r = sd_bus_set_bus_client(login1->bus, 1);
        if (r >= 0)
            r = sd_bus_start(login1->bus);
    } else {
        r = sd_bus_open_system(&login1->bus);
    }
    // polkit may ask for a password before allowing an action (the "challenge" answer).
    if (r >= 0)
        r = sd_bus_set_allow_interactive_authorization(login1->bus, 1);
    if (r >= 0)
        r = sd_bus_match_signal(login1->bus, &login1->sleep_match, LOGIN1, LOGIN1_PATH,
                                LOGIN1_MANAGER, "PrepareForSleep", prepare_for_sleep, login1);
    if (r < 0) {
        snprintf(error, error_size, "cannot connect to %s: %s",
                 address ? address : "the system bus", strerror(-r));
        sd_bus_slot_unref(login1->sleep_match);
        sd_bus_flush_close_unref(login1->bus);
        free(login1);
        return NULL;
    }
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i) {
        login1->asks[i] = (struct pending){login1, (enum sh_login1_method)i, NULL};
        login1->calls[i] = (struct pending){login1, (enum sh_login1_method)i, NULL};
    }
    login1->inhibit.login1 = login1;
    return login1;
}

void sh_login1_destroy(struct sh_login1 *login1) {
    if (!login1)
        return;
    // Dropping the slots first means no reply runs a handler for a caller that has gone.
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i) {
        sd_bus_slot_unref(login1->asks[i].slot);
        sd_bus_slot_unref(login1->calls[i].slot);
    }
    sd_bus_slot_unref(login1->inhibit.slot);
    sd_bus_slot_unref(login1->sleep_match);
    sd_bus_flush_close_unref(login1->bus);
    free(login1);
}

int sh_login1_fd(struct sh_login1 *login1) {
    return sd_bus_get_fd(login1->bus);
}

bool sh_login1_wants_write(struct sh_login1 *login1) {
    int events = sd_bus_get_events(login1->bus);
    return events > 0 && (events & POLLOUT);
}

int sh_login1_timeout(struct sh_login1 *login1) {
    uint64_t deadline;
    if (sd_bus_get_timeout(login1->bus, &deadline) < 0 || deadline == UINT64_MAX)
        return -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t current = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    if (deadline <= current)
        return 0;
    uint64_t ms = (deadline - current + 999) / 1000;
    return ms > INT32_MAX ? INT32_MAX : (int)ms;
}

bool sh_login1_dispatch(struct sh_login1 *login1) {
    int r;
    while ((r = sd_bus_process(login1->bus, NULL)) > 0)
        ;
    return r >= 0;
}

bool sh_login1_ask(struct sh_login1 *login1, enum sh_login1_method method) {
    struct pending *pending = &login1->asks[method];
    if (pending->slot) // the answer on its way will do
        return true;
    char member[32];
    snprintf(member, sizeof(member), "Can%s", method_names[method]);
    return sd_bus_call_method_async(login1->bus, &pending->slot, LOGIN1, LOGIN1_PATH,
                                    LOGIN1_MANAGER, member, asked, pending, "") >= 0;
}

bool sh_login1_call(struct sh_login1 *login1, enum sh_login1_method method) {
    struct pending *pending = &login1->calls[method];
    sd_bus_message *message = NULL;
    pending->slot = sd_bus_slot_unref(pending->slot);
    int r = sd_bus_message_new_method_call(login1->bus, &message, LOGIN1, LOGIN1_PATH,
                                           LOGIN1_MANAGER, method_names[method]);
    if (r >= 0)
        r = sd_bus_message_append(message, "b", 1);
    if (r >= 0)
        r = sd_bus_call_async(login1->bus, &pending->slot, message, called, pending,
                              CALL_TIMEOUT_USEC);
    sd_bus_message_unref(message);
    return r >= 0;
}

bool sh_login1_inhibit_sleep(struct sh_login1 *login1, const char *why) {
    struct pending *pending = &login1->inhibit;
    pending->slot = sd_bus_slot_unref(pending->slot);
    return sd_bus_call_method_async(login1->bus, &pending->slot, LOGIN1, LOGIN1_PATH,
                                    LOGIN1_MANAGER, "Inhibit", inhibited, pending, "ssss",
                                    "sleep", "shaodesk", why, "delay") >= 0;
}
#else
struct sh_login1 *sh_login1_connect(const char *address, const struct sh_login1_handler *handler,
                                    char *error, size_t error_size) {
    (void)address;
    (void)handler;
    snprintf(error, error_size, "shaodesk was built without sd-bus");
    return NULL;
}
void sh_login1_destroy(struct sh_login1 *login1) {
    (void)login1;
}
int sh_login1_fd(struct sh_login1 *login1) {
    (void)login1;
    return -1;
}
bool sh_login1_wants_write(struct sh_login1 *login1) {
    (void)login1;
    return false;
}
int sh_login1_timeout(struct sh_login1 *login1) {
    (void)login1;
    return -1;
}
bool sh_login1_dispatch(struct sh_login1 *login1) {
    (void)login1;
    return false;
}
bool sh_login1_ask(struct sh_login1 *login1, enum sh_login1_method method) {
    (void)login1;
    (void)method;
    return false;
}
bool sh_login1_call(struct sh_login1 *login1, enum sh_login1_method method) {
    (void)login1;
    (void)method;
    return false;
}
bool sh_login1_inhibit_sleep(struct sh_login1 *login1, const char *why) {
    (void)login1;
    (void)why;
    return false;
}
#endif
