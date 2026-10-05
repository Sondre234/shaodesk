/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Keymaps: the one the keyboard settings describe (keyboard.file, else the XKB names), given to
 * every keyboard but the virtual ones, which bring their own. */
#include "server.h"

/* keyboard.file when it is set and compiles (the configuration checked it, but it may have
 * changed since), else the XKB names, else xkbcommon's defaults: never no keymap at all. */
static struct xkb_keymap *compile_keymap(const struct sh_settings *settings) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return NULL;
    struct xkb_keymap *keymap = NULL;
    if (settings->keyboard_file[0]) {
        FILE *file = fopen(settings->keyboard_file, "r");
        if (file) {
            keymap = xkb_keymap_new_from_file(context, file, XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
            fclose(file);
        }
        if (!keymap)
            wlr_log(WLR_ERROR, "Cannot compile the keymap %s; using keyboard.layout instead",
                    settings->keyboard_file);
    }
    struct xkb_rule_names names = {.rules = settings->keyboard_rules,
                                   .layout = settings->keyboard_layout,
                                   .variant = settings->keyboard_variant,
                                   .model = settings->keyboard_model,
                                   .options = settings->keyboard_options};
    if (!keymap)
        keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
        keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(context);
    return keymap;
}

bool configure_keyboard(struct sh_server *server, struct wlr_keyboard *keyboard) {
    const struct sh_settings *settings = server_settings(server);
    struct xkb_keymap *keymap = compile_keymap(settings);
    if (!keymap)
        return false;
    bool ok = wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    wlr_keyboard_set_repeat_info(keyboard, settings->repeat_rate, settings->repeat_delay);
    return ok;
}

/* After a reload: the keymap again, for every keyboard but the virtual ones. */
void reload_keymaps(struct sh_server *server) {
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (wlr_input_device_get_virtual_keyboard(&keyboard->wlr_keyboard->base))
            continue;
        if (!configure_keyboard(server, keyboard->wlr_keyboard))
            wlr_log(WLR_ERROR, "Could not apply reloaded keymap");
    }
}
