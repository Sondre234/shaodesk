# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive workspaces through the control socket and the taskbar protocol."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

with tempfile.TemporaryDirectory(prefix="shaodesk-workspace-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(Path(example).read_text().replace("xwayland = true", "xwayland = false"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def windows():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {row[8]: (int(row[0]), row[1] == "1") for row in rows}

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert msg("get", "workspace") == "1\n"
            assert "needs a workspace from 1 to 4" in msg("workspace", "9", ok=False)
            assert "unknown action" in msg("bogus", ok=False)
            assert "takes no argument" in msg("close", "2", ok=False)

            window = subprocess.Popen([probe, "--external-control"], env=env,
                                      stdout=subprocess.DEVNULL)
            processes.append(window)
            wait_for(lambda: windows().get("shaodesk-probe") == (1, True), processes,
                     "window focused on workspace 1")

            msg("move_to_workspace", "2")
            assert windows()["shaodesk-probe"] == (2, False), windows()
            assert msg("get", "workspace") == "1\n"
            msg("workspace", "2")
            assert msg("get", "workspace") == "2\n"
            assert windows()["shaodesk-probe"] == (2, True), windows()
            msg("workspace_next")
            assert msg("get", "workspace") == "3\n"
            assert windows()["shaodesk-probe"] == (2, False)
            msg("workspace_prev")
            msg("workspace_prev")
            assert msg("get", "workspace") == "1\n"

            # Activating a window from the taskbar switches to its workspace.
            subprocess.run([probe, "--activate", "shaodesk-probe"], env=env, check=True,
                           timeout=30, stdout=subprocess.DEVNULL)
            wait_for(lambda: msg("get", "workspace") == "2\n", processes, "taskbar switch")
            assert windows()["shaodesk-probe"] == (2, True)

            # Keyboard window actions target only the current workspace.
            msg("workspace", "1")
            msg("close")
            assert window.poll() is None, "close reached a window on another workspace"
            msg("workspace", "2")
            msg("close")
            assert window.wait(timeout=30) == 0
            processes.remove(window)
            assert windows() == {}

            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            assert not Path(env["SHAODESK_SOCKET"]).exists(), "control socket left behind"
            print("Workspaces, control socket, taskbar switching, and scoped actions passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
