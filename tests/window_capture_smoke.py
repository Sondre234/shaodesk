# SPDX-License-Identifier: GPL-3.0-or-later
"""Capturing one window, as screen sharing does through ext-foreign-toplevel-list: a session on
the window's capture source copies the window's own surfaces at its size, without the border or
shadow shaodesk draws around it. While the session is locked the source is inert, its session
stopped at once, and the client asking stays connected."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    windows = { border_width = 4, shadow = { enabled = true } },
}"""

# What wayland_probe draws: a dark band along its top, the rest blue.
BAND, BODY = "23314a", "417bc4"

with harness.Compositor(compositor, CONFIG) as desktop:
    def windows():
        """title -> (width, height, minimized)."""
        return {r[9]: (int(r[6]), int(r[7]), r[2] == "1") for r in desktop.rows("windows")}

    desktop.detail = lambda: f"windows: {windows()}"

    def capture(title, *words):
        """What the window probe prints capturing the window titled `title`; it must exit
        cleanly, its connection sound to the end."""
        result = subprocess.run([window_probe, title, *words], env=desktop.env,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout.strip()

    desktop.spawn([probe, "--window-only"],
                  env={"SHAODESK_PROBE_TITLE": "A", "SHAODESK_PROBE_APP_ID": "app-A"})
    desktop.wait_for(lambda: "A" in windows(), "A mapped")
    width, height, _ = windows()["A"]

    # The window as it draws itself: its size, its band at the top-left corner where the border
    # would be, and its body in the middle.
    assert capture("A", "capture-listed") == f"{width}x{height} {BAND} {BODY}", \
        capture("A", "capture-listed")

    # Locked, the window is not to be seen: the source is inert, its session stops at once, and
    # the probe goes on to its last roundtrip without a protocol error.
    calls = desktop.root / "locker.log"
    locker = desktop.spawn([lock_probe, "hold", str(calls)])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    assert capture("A", "capture-listed") == "stopped"
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    assert capture("A", "capture-listed") == f"{width}x{height} {BAND} {BODY}"
print("A window's capture shows its own surfaces, and is inert while the session is locked")
