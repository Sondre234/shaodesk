# SPDX-License-Identifier: GPL-3.0-or-later
"""focus_last focuses the window focused before the current one, flipping between two windows
when repeated, also across workspaces."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = { { mods = { "Alt" }, key = "grave", action = "focus_last" } },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-focus-last-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", minimized=r[2] == "1")
                for r in rows}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def workspace():
        return int(msg("get", "workspace"))

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            msg("focus_last")  # no windows: nothing happens
            for title in ("A", "B", "C"):
                processes.append(subprocess.Popen([probe, "--window-only"],
                                                  env=dict(env, SHAODE_PROBE_TITLE=title),
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: focused() == title, f"{title} focused")

            # C was focused after B: the history is C, B, A.
            msg("focus_last")
            wait_for(lambda: focused() == "B", "back to B")
            msg("focus_last")
            wait_for(lambda: focused() == "C", "flip back to C")
            msg("focus_last")
            wait_for(lambda: focused() == "B", "flip to B again")

            # The history follows what is focused: B, C, A.
            msg("focus_last")
            wait_for(lambda: focused() == "C", "C")

            # Across workspaces: B moves to workspace 3, where focus_last follows it.
            msg("workspace", "1")
            msg("focus_last")
            wait_for(lambda: focused() == "B", "B focused")
            msg("move_to_workspace", "3")
            wait_for(lambda: windows()["B"]["workspace"] == 3, "B on workspace 3")
            wait_for(lambda: focused() != "B", "B lost focus")
            first = focused()
            msg("focus_last")
            wait_for(lambda: focused() == "B" and workspace() == 3, "B, on its workspace")
            msg("focus_last")
            wait_for(lambda: focused() == first and workspace() == 1, "back to the first")

            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("focus_last passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
