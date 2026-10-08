# SPDX-License-Identifier: GPL-3.0-or-later
"""Snapping from the keyboard: the quarter actions put the focused window into a quarter of its
monitor, and restore puts it back."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, gap = 0 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, minimized, output."""
        return {r[9]: (int(r[4]), int(r[5]), int(r[6]), int(r[7]), r[2] == "1", r[10])
                for r in desktop.rows("windows")}

    def placed(box, title="W"):
        desktop.wait_for(lambda: windows()[title][:4] == tuple(box), f"{title} at {box}")

    desktop.detail = lambda: f"windows: {windows()}"
    desktop.spawn([probe, "--window-only"], env={"SHAODESK_PROBE_TITLE": "W"})
    desktop.wait_for(lambda: "W" in windows(), "window mapped")
    floating = windows()["W"][:4]
    assert floating[2:] == (320, 240) and windows()["W"][5] == "HEADLESS-1", windows()

    # The quarters, each from the last, and back.
    for action, box in [("snap_top_left", (0, 0, 640, 360)),
                        ("snap_top_right", (640, 0, 640, 360)),
                        ("snap_bottom_left", (0, 360, 640, 360)),
                        ("snap_bottom_right", (640, 360, 640, 360))]:
        msg(action)
        placed(box)
    msg("restore")
    placed(floating)
print("Snapping from the keyboard passed")
