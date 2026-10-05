# SPDX-License-Identifier: GPL-3.0-or-later
"""Workspaces named in layout.workspace_names are reached by name from the control socket and
from bindings; an unknown name is refused."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4, workspace_names = { "web", "code", "", "chat room" } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-workspace-names-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout.strip() if ok else result.stderr

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert msg("get", "workspace") == "1"
            msg("workspace", "code")
            assert msg("get", "workspace") == "2"
            msg("workspace", "chat", "room")
            assert msg("get", "workspace") == "4"
            msg("workspace", "web")
            assert msg("get", "workspace") == "1"
            msg("workspace", "3")
            assert msg("get", "workspace") == "3"
            assert "workspace name" in msg("workspace", "nope", ok=False)
            assert "workspace name" in msg("workspace", ok=False)
            assert "workspace name" in msg("move_to_workspace", "9", ok=False)
            assert msg("get", "workspace") == "3"
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("workspace names passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
