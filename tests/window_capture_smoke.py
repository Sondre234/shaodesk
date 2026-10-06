# SPDX-License-Identifier: GPL-3.0-or-later
"""Capturing one window, as the taskbar's pictures do through shaodesk-window-control-v1's
get_capture_source and screen sharing through ext-foreign-toplevel-list: a session on the
window's capture source copies the window's own surfaces at its size, without the border or
shadow shaodesk draws around it, also while it is minimized or on a workspace not shown; it
follows the window as it redraws and stops as it closes. While the session is locked the source
is inert, its session stopped at once, and the client asking stays connected."""
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

import harness

compositor, probe, window_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    windows = { border_width = 4, shadow = { enabled = true } },
}"""

# What wayland_probe draws: a dark band along its top, the rest blue.
BAND, BODY = "23314a", "417bc4"
# The two ways to a window's capture source: the taskbar's, by its wlr-foreign-toplevel handle,
# and screen sharing's, by its ext-foreign-toplevel-list handle.
PATHS = ("capture", "capture-listed")

with harness.Compositor(compositor, CONFIG) as desktop:
    def windows():
        """title -> (width, height, minimized, visible, focused)."""
        return {r[9]: (int(r[6]), int(r[7]), r[2] == "1", r[11] == "1", r[1] == "1")
                for r in desktop.rows("windows")}

    desktop.detail = lambda: f"windows: {windows()}"

    def run(title, *words):
        """What the window probe prints for the window titled `title`; it must exit cleanly, its
        connection sound to its last roundtrip."""
        result = subprocess.run([window_probe, title, *words], env=desktop.env,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout.strip()

    def picture(width, height):
        """The probe's line for a frame of the probe's window at that size."""
        return f"{width}x{height} {BAND} {BODY}"

    def captures(title, expected):
        """Both ways to the window's capture source give one frame, as `expected`."""
        for path in PATHS:
            assert run(title, path) == expected, (path, run(title, path))

    a = desktop.spawn([probe, "--window-only"],
                      env={"SHAODESK_PROBE_TITLE": "A", "SHAODESK_PROBE_APP_ID": "app-A"})
    desktop.wait_for(lambda: "A" in windows(), "A mapped")
    width, height = windows()["A"][:2]

    # The window as it draws itself: its size, its band at the top-left corner where the border
    # would be, and its body in the middle.
    captures("A", picture(width, height))

    # Minimized, and on a workspace not shown, it is still there to capture.
    run("A", "minimize")
    desktop.wait_for(lambda: windows()["A"][2], "A minimized")
    captures("A", picture(width, height))
    subprocess.run([probe, "--activate", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: not windows()["A"][2], "A restored from the taskbar")
    run("A", "workspace", "2")
    desktop.wait_for(lambda: not windows()["A"][3], "A on workspace 2")
    captures("A", picture(width, height))

    # Locked, the window is not to be seen: the source is inert, its session stops at once, and
    # the probe goes on to its last roundtrip without a protocol error.
    calls = desktop.root / "locker.log"
    locker = desktop.spawn([lock_probe, "hold", str(calls)])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    captures("A", "stopped")
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    captures("A", picture(width, height))

    # A session follows the window, a frame each time it redraws, at its new size when that
    # changes. The watching probe prints a line for each frame.
    subprocess.run([probe, "--activate", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["A"][3:] == (True, True), "A focused, and shown")
    watch = desktop.spawn([window_probe, "A", "capture", "watch"], stdout=subprocess.PIPE,
                          text=True)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.strip()) for line in watch.stdout],
                     daemon=True).start()

    def heard(expected):
        """Waits for the watching probe to print `expected`, past the frames before it."""
        deadline, seen = time.monotonic() + 15, []
        while time.monotonic() < deadline:
            try:
                seen.append(lines.get(timeout=.1))
            except queue.Empty:
                continue
            if seen[-1] == expected:
                return
        raise AssertionError(f"the watching probe printed {seen}, not {expected!r}")

    heard(picture(width, height))
    desktop.msg("maximize")
    desktop.wait_for(lambda: windows()["A"][:2] != (width, height), "A maximized")
    heard(picture(*windows()["A"][:2]))
    desktop.msg("restore")
    desktop.wait_for(lambda: windows()["A"][:2] == (width, height), "A restored")
    heard(picture(width, height))

    # Closing the window stops the session, and the watching probe exits cleanly.
    subprocess.run([probe, "--close", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(a) == 0
    heard("stopped")
    assert desktop.reap(watch) == 0
print("A window's capture shows its own surfaces as they change, minimized or out of sight too, "
      "stops as it closes, and is inert while the session is locked")
