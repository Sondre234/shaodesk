/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows' own icons: the one a window supplies, kept on it as a name and the pixels of one size
 * for the window control to send the shell. */
#include "server.h"

/* How well an icon of this many pixels suits the shell, higher better and 0 for one to pass
 * over: the largest that fits within 256 by 256, else the smallest of the larger ones. An icon
 * is measured by its longer side; one wider or taller than 4096 is passed over too. */
int icon_size_rank(uint32_t width, uint32_t height) {
    uint32_t side = width > height ? width : height;
    if (!width || !height || side > 4096)
        return 0;
    return side <= 256 ? 8192 + (int)side : 8192 - (int)side;
}

void free_icon(struct sh_icon *icon) {
    free(icon->name);
    free(icon->pixels);
    *icon = (struct sh_icon){0};
}

static bool same_icon(const struct sh_icon *a, const struct sh_icon *b) {
    if ((a->name == NULL) != (b->name == NULL) || (a->name && strcmp(a->name, b->name)))
        return false;
    if ((a->pixels == NULL) != (b->pixels == NULL))
        return false;
    return !a->pixels || (a->width == b->width && a->height == b->height &&
                          !memcmp(a->pixels, b->pixels, (size_t)a->width * a->height * 4));
}

/* Gives the window `icon`, which it takes over, an empty one to drop the icon it has. The window
 * control's objects hear of a change; an icon the same as the one the window has is no change,
 * so that a client setting it again, as an X11 window's is read again as it maps, costs them
 * nothing. */
void set_toplevel_icon(struct sh_toplevel *toplevel, struct sh_icon icon) {
    if (same_icon(&toplevel->icon, &icon)) {
        free_icon(&icon);
        return;
    }
    free_icon(&toplevel->icon);
    toplevel->icon = icon;
    ++toplevel->icon_serial;
    window_objects_changed(toplevel->server);
}
