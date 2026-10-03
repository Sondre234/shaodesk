# SPDX-License-Identifier: GPL-3.0-or-later
"""Sticky windows: shown on every workspace of their output, and unstuck by moving them to one
workspace or by turning features.sticky off."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

with tempfile.TemporaryDirectory(prefix="shaode-sticky-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    source = (Path(example).read_text().replace("xwayland = true", "xwayland = false")
              .replace("tiling = false,", "tiling = true,", 1))
    assert "sticky = true," in source
    config.write_text(source)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def window():
        """(workspace, focused, tiled, visible, sticky) of the only window."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        assert len(rows) == 1 and len(rows[0]) == 15, rows
        row = rows[0]
        return (int(row[0]), row[1] == "1", row[3] == "1", row[11] == "1", row[13] == "1")

    def workspace():
        return int(msg("get", "workspace"))

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert "takes no argument" in msg("toggle_sticky", "1", ok=False)

            client = subprocess.Popen([probe, "--external-control"], env=env,
                                      stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: msg("get", "windows").count("\n") == 1 and window()[2], processes,
                     "window tiled on workspace 1")
            assert window() == (1, True, True, True, False), window()

            # A sticky tile floats and follows every workspace switch, keeping focus.
            msg("toggle_sticky")
            assert window() == (1, True, False, True, True), window()
            msg("workspace", "2")
            assert workspace() == 2
            assert window() == (2, True, False, True, True), window()
            msg("workspace_next")
            assert window() == (3, True, False, True, True), window()
            assert msg("get", "workspaces").split("\t")[3] == "3", msg("get", "workspaces")

            # Activating it from the taskbar leaves the workspace alone.
            subprocess.run([probe, "--activate", "shaode-probe"], env=env, check=True,
                           timeout=30, stdout=subprocess.DEVNULL)
            assert workspace() == 3 and window() == (3, True, False, True, True), window()

            # Unsticking it leaves it on this workspace, tiled again as before.
            msg("toggle_sticky")
            assert window() == (3, True, True, True, False), window()
            msg("workspace", "1")
            assert window() == (3, False, True, False, False), window()
            msg("workspace", "3")

            # Moving a sticky window to a workspace unsticks it there.
            msg("toggle_sticky")
            msg("workspace", "2")
            assert window() == (2, True, False, True, True), window()
            msg("move_to_workspace", "4")
            assert window() == (4, False, True, False, False), window()
            assert workspace() == 2
            msg("workspace", "4")
            assert window() == (4, True, True, True, False), window()

            # Turning the feature off unsticks windows where they are, and disables the action.
            msg("toggle_sticky")
            msg("workspace", "1")
            assert window() == (1, True, False, True, True), window()
            config.write_text(source.replace("sticky = true,", "sticky = false,"))
            msg("reload")
            assert "Configuration reloaded" in log.read_text()
            assert window() == (1, True, True, True, False), window()
            msg("toggle_sticky")
            assert window() == (1, True, True, True, False), window()
            msg("workspace", "2")
            assert window() == (1, False, True, False, False), window()

            # And back on, the action works again.
            config.write_text(source)
            msg("reload")
            msg("workspace", "1")
            msg("toggle_sticky")
            assert window() == (1, True, False, True, True), window()

            msg("close")
            assert client.wait(timeout=30) == 0
            processes.remove(client)
            assert msg("get", "windows") == ""

            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Sticky windows follow workspaces, unstick on move, and obey features.sticky")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
