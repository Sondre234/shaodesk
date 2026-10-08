/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Input devices without hardware, for tests under --headless, as headless keyboards are
 * (input.c): pointers that move and make touchpad gestures, which no virtual-pointer client can
 * send. They are devices like any other once plugged in. */
#include "server.h"
#include <wlr/interfaces/wlr_pointer.h>

/* Splits `arguments` into at most `size` words in `words`, each at most 63 bytes; returns how
 * many there were, or size + 1 when there were more. */
static int split_words(const char *arguments, char words[][64], int size) {
    int count = 0;
    const char *at = arguments;
    while (*at) {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        size_t length = strcspn(at, " ");
        if (count == size || length >= 64)
            return size + 1;
        snprintf(words[count++], 64, "%.*s", (int)length, at);
        at += length;
    }
    return count;
}

/* A number from a word: false when the word is not wholly one. */
static bool parse_double(const char *word, double *value) {
    char *end = NULL;
    *value = strtod(word, &end);
    return end != word && !*end && isfinite(*value);
}

static bool parse_unsigned(const char *word, unsigned *value) {
    char *end = NULL;
    unsigned long number = strtoul(word, &end, 10);
    if (end == word || *end || word[0] == '-' || number > UINT32_MAX)
        return false;
    *value = (unsigned)number;
    return true;
}

/* The event time a command gives as its last word, else now. */
static bool event_time(char words[][64], int count, int at, uint32_t *time) {
    unsigned given = 0;
    if (count <= at) {
        *time = (uint32_t)now_ms();
        return true;
    }
    if (count > at + 1 || !parse_unsigned(words[at], &given))
        return false;
    *time = given;
    return true;
}

/* Pointers: "headless_pointer add NAME", "headless_pointer remove NAME",
 * "headless_pointer move NAME X Y" (layout coordinates), and gestures, each with an optional
 * event time in milliseconds last, so that a test can say how fast fingers move:
 *   headless_pointer swipe|pinch|hold NAME begin FINGERS [TIME]
 *   headless_pointer swipe NAME update DX DY [TIME]
 *   headless_pointer pinch NAME update DX DY SCALE ROTATION [TIME]
 *   headless_pointer swipe|pinch|hold NAME end|cancel [TIME] */
struct sh_headless_pointer {
    struct wlr_pointer pointer;
    uint32_t fingers; // of the swipe or pinch under way
    struct wl_list link; // sh_server.headless_pointers
};
static const struct wlr_pointer_impl headless_pointer_impl = {.name = "headless-pointer"};

static struct sh_headless_pointer *find_headless_pointer(struct sh_server *server,
                                                         const char *name) {
    struct sh_headless_pointer *pointer;
    wl_list_for_each(pointer, &server->headless_pointers, link) {
        if (!strcmp(pointer->pointer.base.name, name))
            return pointer;
    }
    return NULL;
}

static void remove_headless_pointer(struct sh_headless_pointer *pointer) {
    wl_list_remove(&pointer->link);
    wlr_pointer_finish(&pointer->pointer); // unplugs it
    free(pointer);
}

/* Runs a gesture command (words from the verb on); false when it is not a valid one. */
static bool headless_gesture(struct sh_headless_pointer *headless, char words[][64], int count) {
    struct wlr_pointer *pointer = &headless->pointer;
    const char *kind = words[0], *step = words[2];
    uint32_t time = 0;
    unsigned fingers = 0;
    if (!strcmp(step, "begin")) {
        if (count < 4 || !parse_unsigned(words[3], &fingers) || fingers < 1 || fingers > 10 ||
            !event_time(words, count, 4, &time))
            return false;
        headless->fingers = fingers;
        if (!strcmp(kind, "swipe")) {
            struct wlr_pointer_swipe_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.swipe_begin, &event);
        } else if (!strcmp(kind, "pinch")) {
            struct wlr_pointer_pinch_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.pinch_begin, &event);
        } else {
            struct wlr_pointer_hold_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.hold_begin, &event);
        }
        return true;
    }
    if (!strcmp(step, "end") || !strcmp(step, "cancel")) {
        if (!event_time(words, count, 3, &time))
            return false;
        bool cancelled = !strcmp(step, "cancel");
        if (!strcmp(kind, "swipe")) {
            struct wlr_pointer_swipe_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.swipe_end, &event);
        } else if (!strcmp(kind, "pinch")) {
            struct wlr_pointer_pinch_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.pinch_end, &event);
        } else {
            struct wlr_pointer_hold_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.hold_end, &event);
        }
        return true;
    }
    double dx, dy, scale, rotation;
    if (strcmp(step, "update") || count < 5 || !parse_double(words[3], &dx) ||
        !parse_double(words[4], &dy))
        return false;
    if (!strcmp(kind, "swipe")) {
        if (!event_time(words, count, 5, &time))
            return false;
        struct wlr_pointer_swipe_update_event event = {pointer, time, headless->fingers, dx, dy};
        wl_signal_emit_mutable(&pointer->events.swipe_update, &event);
        return true;
    }
    if (strcmp(kind, "pinch") || count < 7 || !parse_double(words[5], &scale) ||
        !parse_double(words[6], &rotation) || !event_time(words, count, 7, &time))
        return false;
    struct wlr_pointer_pinch_update_event event = {pointer, time, headless->fingers, dx, dy,
                                                   scale, rotation};
    wl_signal_emit_mutable(&pointer->events.pinch_update, &event);
    return true;
}

void control_headless_pointer(struct sh_server *server, int fd, const char *arguments) {
    static const char *usage =
        "error: usage: headless_pointer add NAME | remove NAME | move NAME X Y | "
        "swipe|pinch|hold NAME begin FINGERS [TIME] | swipe NAME update DX DY [TIME] | "
        "pinch NAME update DX DY SCALE ROTATION [TIME] | swipe|pinch|hold NAME end|cancel "
        "[TIME]\n";
    if (!headless_backend(server)) {
        control_reply(fd, "error: headless_pointer needs --headless\n");
        return;
    }
    char words[8][64];
    int count = split_words(arguments, words, 8);
    if (count < 2 || count > 8) {
        control_reply(fd, usage);
        return;
    }
    const char *verb = words[0], *name = words[1];
    struct sh_headless_pointer *pointer = find_headless_pointer(server, name);
    if (!strcmp(verb, "add") && count == 2) {
        if (pointer) {
            control_reply(fd, "error: a pointer with that name exists\n");
            return;
        }
        pointer = calloc(1, sizeof(*pointer));
        if (!pointer) {
            control_reply(fd, "error: out of memory\n");
            return;
        }
        wlr_pointer_init(&pointer->pointer, &headless_pointer_impl, name);
        wl_list_insert(&server->headless_pointers, &pointer->link);
        server_new_input(&server->new_input, &pointer->pointer.base);
        control_reply(fd, "ok\n");
        return;
    }
    bool known = !strcmp(verb, "remove") || !strcmp(verb, "move") || !strcmp(verb, "swipe") ||
                 !strcmp(verb, "pinch") || !strcmp(verb, "hold");
    if (!known) {
        control_reply(fd, usage);
        return;
    }
    if (!pointer) {
        control_reply(fd, "error: no such pointer\n");
        return;
    }
    if (!strcmp(verb, "remove") && count == 2) {
        remove_headless_pointer(pointer);
        control_reply(fd, "ok\n");
        return;
    }
    if (!strcmp(verb, "move")) {
        double x, y;
        struct wlr_box layout;
        wlr_output_layout_get_box(server->output_layout, NULL, &layout);
        if (count != 4 || !parse_double(words[2], &x) || !parse_double(words[3], &y) ||
            wlr_box_empty(&layout)) {
            control_reply(fd, usage);
            return;
        }
        struct wlr_pointer_motion_absolute_event event = {
            .pointer = &pointer->pointer,
            .time_msec = (uint32_t)now_ms(),
            .x = (x - layout.x) / layout.width,
            .y = (y - layout.y) / layout.height,
        };
        wl_signal_emit_mutable(&pointer->pointer.events.motion_absolute, &event);
        wl_signal_emit_mutable(&pointer->pointer.events.frame, &pointer->pointer);
        control_reply(fd, "ok\n");
        return;
    }
    if (strcmp(verb, "remove") && count >= 3 && headless_gesture(pointer, words, count)) {
        control_reply(fd, "ok\n");
        return;
    }
    control_reply(fd, usage);
}

void destroy_headless_inputs(struct sh_server *server) {
    struct sh_headless_pointer *pointer, *temporary;
    wl_list_for_each_safe(pointer, temporary, &server->headless_pointers, link)
        remove_headless_pointer(pointer);
}
