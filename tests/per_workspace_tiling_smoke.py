# SPDX-License-Identifier: GPL-3.0-or-later
"""With layout.tiling_per_workspace, toggling tiling changes only the current workspace, windows
moved between workspaces tile or float as their new workspace does, and turning the setting off
puts every workspace back on the output's setting."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(per_workspace=True):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, tiling_per_workspace = {str(per_workspace).lower()} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaodesk-per-workspace-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(config())
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """(workspace, tiled) per window, oldest first."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return [(int(row[0]), row[3] == "1") for row in rows]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"windows: {windows()}, tiling: {msg('get', 'tiling')}")

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        init.write_text(text)
        server.send_signal(signal.SIGHUP)
        wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads, "reload")

    def launch():
        count = len(windows()) + 1
        processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                          stdout=subprocess.DEVNULL))
        wait_for(lambda: len(windows()) == count, f"window {count} mapped")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            launch()
            launch()
            wait_for(lambda: windows() == [(1, True)] * 2, "tiled on workspace 1")

            # Workspace 2 starts as the output is, and toggles on its own.
            msg("workspace", "2")
            launch()
            wait_for(lambda: windows()[2] == (2, True), "tiled on workspace 2")
            msg("toggle_tiling")
            wait_for(lambda: windows()[2] == (2, False), "floating on workspace 2")
            assert msg("get", "tiling") == "off\n"
            assert windows()[:2] == [(1, True)] * 2, windows()
            msg("workspace", "1")
            assert msg("get", "tiling") == "on\n", "workspace 1 should still tile"

            # A tile moved to workspace 2 floats there; a window moved back tiles again.
            msg("move_to_workspace", "2")
            wait_for(lambda: sorted(windows()) == [(1, True), (2, False), (2, False)],
                     "moved tile floating on workspace 2")
            msg("workspace", "2")
            msg("move_to_workspace", "1")
            wait_for(lambda: sorted(windows()) == [(1, True), (1, True), (2, False)],
                     "moved window tiled on workspace 1")

            # Without the setting every workspace follows the output, and toggling changes all.
            reload(config(per_workspace=False))
            wait_for(lambda: windows() == [(w, True) for w, _ in windows()],
                     "every workspace tiled by the output's setting")
            msg("toggle_tiling")
            wait_for(lambda: not any(tiled for _, tiled in windows()), "every window floating")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Per-workspace tiling toggles, moves, and reloads passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
