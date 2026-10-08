# SPDX-License-Identifier: GPL-3.0-or-later
"""The display mode popup end to end, from the XF86Display key on a headless compositor with two
monitors to the shell: the popup shows on the focused monitor, a click on a choice takes it at once
(the other monitor then mirrors the main one, leaving the layout and the shell's screens), and it
closes."""
from pathlib import Path
import sys

import harness

compositor, shell, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    outputs = { primary = "HEADLESS-1", order = { "HEADLESS-1", "HEADLESS-2" } },
    bindings = { { key = "XF86Display", action = "display_mode" } },
}"""

with harness.Compositor(compositor, CONFIG, start=False,
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"
    desktop.start()

    def log():
        return shell_log.read_text()

    def outputs():
        """name: (in the layout, mirrored) per monitor."""
        return {r[0]: (r[1] == "1", r[11]) for r in desktop.rows("outputs")}

    desktop.detail = lambda: f"outputs: {outputs()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: log().count("shaodesk surface rendered: shaodesk taskbar") >= 2,
                     "the panels rendered", timeout=30)
    pointer = desktop.virtual_pointer(pointer_probe, 2560, 720)
    pointer("move 640 200")  # the first monitor has the focus

    # The shell subscribes asynchronously: press until the popup shows.
    msg("headless_keyboard", "add", "keys")
    for _ in range(20):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", "227", state)
        try:
            desktop.wait_for(lambda: "shaodesk display mode shown on HEADLESS-1" in log(),
                             "the popup shown", timeout=1)
            break
        except harness.Timeout:
            msg("headless_keyboard", "key", "keys", "1", "press")  # Escape: closed again
            msg("headless_keyboard", "key", "keys", "1", "release")
    assert desktop.rows("display_mode")[0] == ["extend", "extend", "HEADLESS-1"]

    # In the middle of the output, the second of the four tiles of 112 by 92 pixels, 4 apart, is
    # Duplicate; a click on it takes it before the key's time is up.
    pointer("move 582 372")
    pointer("click left")
    desktop.wait_for(lambda: outputs() == {"HEADLESS-1": (True, "-"),
                                           "HEADLESS-2": (False, "HEADLESS-1")},
                     "the click took duplicate")
    desktop.wait_for(lambda: "shaodesk display mode hidden on HEADLESS-1" in log(),
                     "the popup closed")
    assert desktop.rows("display_mode")[0] == ["duplicate", "-", "-"]
print("The display mode popup shows in the shell, and a click takes a choice")
