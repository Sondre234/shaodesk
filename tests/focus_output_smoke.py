# SPDX-License-Identifier: GPL-3.0-or-later
"""focus_left/right, as sway's focus: with no window that way, focus moves on to the next
output, so an empty output can be reached from the keyboard and new windows open there."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 sits left of HEADLESS-2.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-focus-output-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """(focused, output) per window, by output (one window per output here)."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return sorted(((r[1] == "1", r[10]) for r in rows), key=lambda w: w[1])

    def focused_output():
        rows = [line.split("\t") for line in msg("get", "workspaces").splitlines()]
        return next(r[0] for r in rows if r[2] == "1")

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"windows: {windows()}, output: {focused_output()}")

    def open_window():
        count = len(windows())
        processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                          stdout=subprocess.DEVNULL))
        wait_for(lambda: len(windows()) == count + 1, f"window {count + 1} mapped")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            open_window()
            wait_for(lambda: windows() == [(True, "HEADLESS-1")], "A focused on HEADLESS-1")

            # No window to the right: focus moves to the empty HEADLESS-2, and the next window
            # opens there.
            msg("focus_right")
            wait_for(lambda: focused_output() == "HEADLESS-2" and not windows()[0][0],
                     "empty HEADLESS-2 focused")
            open_window()
            wait_for(lambda: windows()[1] == (True, "HEADLESS-2"), "B focused on HEADLESS-2")

            # Between windows on the two outputs, as before.
            msg("focus_left")
            wait_for(lambda: windows() == [(True, "HEADLESS-1"), (False, "HEADLESS-2")],
                     "A focused")
            msg("focus_right")
            wait_for(lambda: windows() == [(False, "HEADLESS-1"), (True, "HEADLESS-2")],
                     "B focused")
            # Past the last output nothing happens.
            msg("focus_right")
            assert windows()[1][0] and focused_output() == "HEADLESS-2", windows()

            # From an empty output with nothing focused, back to the window on the other.
            processes.pop().kill()
            wait_for(lambda: windows() == [(True, "HEADLESS-1")], "B closed")
            msg("focus_right")
            wait_for(lambda: focused_output() == "HEADLESS-2" and not windows()[0][0],
                     "empty HEADLESS-2 focused again")
            msg("focus_left")
            wait_for(lambda: windows() == [(True, "HEADLESS-1")], "A focused from empty output")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=5)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Focus moved between windows and onto an empty output")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
