# SPDX-License-Identifier: GPL-3.0-or-later
"""shaodesk-window-control-v1, as the taskbar's window menu uses it: a window named by its
taskbar handle hears its output, workspace, placement and process, and moves to another workspace
or output, becomes sticky or floats, without taking the focus from the window that has it."""
from pathlib import Path
import queue
import subprocess
import sys
import threading

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = 0, workspaces = 4 },
    features = { sticky = true },
    outputs = {
        primary = "HEADLESS-1",
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720", tiling = false },
        },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        """title -> (workspace, focused, tiled, output, visible, sticky)."""
        return {r[9]: (int(r[0]), r[1] == "1", r[3] == "1", r[10], r[11] == "1", r[13] == "1")
                for r in desktop.rows("windows")}

    desktop.detail = lambda: f"windows: {windows()}"

    def control(title, *words):
        """What the probe prints for the window titled `title`, after sending `words`."""
        return subprocess.run([window_probe, title, *words], env=desktop.env, check=True,
                              capture_output=True, text=True, timeout=30).stdout.strip()

    def launch(title):
        client = desktop.spawn([probe, "--window-only"],
                               env={"SHAODESK_PROBE_TITLE": title,
                                    "SHAODESK_PROBE_APP_ID": f"app-{title}"})
        desktop.wait_for(lambda: title in windows() and windows()[title][2], f"{title} tiled")
        return client

    a = launch("A")
    b = launch("B")
    assert control("A") == "HEADLESS-1 1 tiled,tiling", control("A")
    # Each window's process, as its client is the probe started for it.
    assert control("A", "pid") == str(a.pid), (control("A", "pid"), a.pid)
    assert control("B", "pid") == str(b.pid), (control("B", "pid"), b.pid)
    assert windows()["B"][1], "the newest window has the focus"

    # A window the probe watches hears each change as it ends up, once.
    watch = desktop.spawn([window_probe, "A", "watch"], stdout=subprocess.PIPE, text=True)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.strip()) for line in watch.stdout],
                     daemon=True).start()

    def heard():
        """The next state the watching probe printed."""
        return lines.get(timeout=15)

    assert heard() == "HEADLESS-1 1 tiled,tiling"

    # To another workspace of its output: it leaves the tiling here for the one there, out of
    # sight, and B keeps the focus.
    control("A", "workspace", "3")
    assert windows()["A"] == (3, False, True, "HEADLESS-1", False, False), windows()
    assert windows()["B"][1]
    assert heard() == "HEADLESS-1 3 tiled,tiling"
    control("A", "workspace", "9")  # past the configured workspaces
    control("A", "workspace", "0")
    assert windows()["A"][0] == 3

    # Floating and tiling again, where the workspace tiles.
    control("A", "floating", "1")
    assert windows()["A"][2] is False
    assert control("A") == "HEADLESS-1 3 floating,tiling"
    assert heard() == "HEADLESS-1 3 floating,tiling"
    control("A", "floating", "0")
    assert windows()["A"][2] is True
    assert heard() == "HEADLESS-1 3 tiled,tiling"

    # Sticky: it floats and shows on the workspace its output shows, following it.
    control("A", "sticky", "1")
    assert windows()["A"] == (1, False, False, "HEADLESS-1", True, True), windows()
    assert heard() == "HEADLESS-1 1 sticky,floating,tiling"
    msg("output", "HEADLESS-1", "workspace", "2")
    assert windows()["A"][0] == 2 and windows()["A"][4]
    assert heard() == "HEADLESS-1 2 sticky,floating,tiling"
    control("A", "sticky", "0")  # focused now, the only window shown
    assert windows()["A"] == (2, True, True, "HEADLESS-1", True, False), windows()
    assert heard() == "HEADLESS-1 2 tiled,tiling"
    msg("output", "HEADLESS-1", "workspace", "1")

    # To the other output, which does not tile: the tile floats on the workspace shown there,
    # and B, focused on the first output, keeps the focus.
    control("B", "output", "HEADLESS-2")
    assert windows()["B"] == (1, True, False, "HEADLESS-2", True, False), windows()
    assert control("B") == "HEADLESS-2 1 -", control("B")
    control("B", "output", "HEADLESS-9")  # no such output
    assert windows()["B"][3] == "HEADLESS-2"
    # And back, into the tiling there.
    control("B", "output", "HEADLESS-1")
    assert windows()["B"] == (1, True, True, "HEADLESS-1", True, False), windows()

    # A window out of sight on another workspace shows on the one the other output shows, the
    # focus staying with B.
    control("A", "output", "HEADLESS-2")
    assert windows()["A"] == (1, False, False, "HEADLESS-2", True, False), windows()
    assert windows()["B"][1]
    assert heard() == "HEADLESS-2 1 -"

    # Turning the feature off leaves set_sticky without effect.
    desktop.reload(CONFIG.replace("sticky = true", "sticky = false"))
    control("B", "sticky", "1")
    assert not windows()["B"][5]

    # Closing the window ends the watch, its object going inert, and nothing else is heard.
    subprocess.run([probe, "--close", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(a) == 0 and desktop.reap(watch) == 0
    assert "A" not in windows()
    assert lines.empty(), lines.get()
print("Windows named by their taskbar handles move, float, stick, and report where they are")
