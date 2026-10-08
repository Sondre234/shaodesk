/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The keyboard's volume, microphone and brightness keys (volume_up, volume_down, volume_mute,
 * mic_mute, brightness_up, brightness_down): the shell carries them out on the sound server and
 * the backlight, and shows them on its on-screen display. */
#include "server.h"

/* Tells the shell: "volume up|down PERCENT", "volume mute", "microphone mute", "brightness
 * up|down PERCENT". */
void volume_action(struct sh_server *server, enum sh_action action, int percent) {
    char line[64];
    switch (action) {
    case SH_VOLUME_UP:
    case SH_VOLUME_DOWN:
        snprintf(line, sizeof(line), "volume %s %d\n", action == SH_VOLUME_UP ? "up" : "down",
                 percent);
        break;
    case SH_VOLUME_MUTE:
        snprintf(line, sizeof(line), "volume mute\n");
        break;
    case SH_MIC_MUTE:
        snprintf(line, sizeof(line), "microphone mute\n");
        break;
    case SH_BRIGHTNESS_UP:
    case SH_BRIGHTNESS_DOWN:
        snprintf(line, sizeof(line), "brightness %s %d\n",
                 action == SH_BRIGHTNESS_UP ? "up" : "down", percent);
        break;
    default:
        return;
    }
    send_shell_line(server, line);
}
