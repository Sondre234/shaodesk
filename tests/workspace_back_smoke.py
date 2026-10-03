# SPDX-License-Identifier: GPL-3.0-or-later
"""workspace_back returns each monitor to its previous workspace, and
features.workspace_back_and_forth makes `workspace N` for the shown workspace do the same."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    features = { workspace_back_and_forth = %s },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-workspace-back-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG % "false")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def current():
        rows = [line.split("\t") for line in msg("get", "workspaces").splitlines()]
        return {row[0]: int(row[1]) for row in rows}

    def on(output, *words):
        msg("output", output, *words)
        return current()[output]

    def windows():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {row[8]: (int(row[0]), row[10], row[11] == "1") for row in rows}

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            wait_for(lambda: len(current()) == 2, processes, "two outputs")
            assert "takes no argument" in msg("workspace_back", "2", ok=False)

            # Nothing to go back to yet.
            assert on("HEADLESS-1", "workspace_back") == 1
            assert on("HEADLESS-1", "workspace", "3") == 3
            assert on("HEADLESS-1", "workspace_back") == 1
            assert on("HEADLESS-1", "workspace_back") == 3
            # Without the feature, naming the shown workspace stays there.
            assert on("HEADLESS-1", "workspace", "3") == 3
            # workspace_next and workspace_prev count as switches too.
            assert on("HEADLESS-1", "workspace_next") == 4
            assert on("HEADLESS-1", "workspace_back") == 3
            # Each monitor keeps its own.
            assert on("HEADLESS-2", "workspace", "2") == 2
            assert on("HEADLESS-2", "workspace_back") == 1
            assert current() == {"HEADLESS-1": 3, "HEADLESS-2": 1}, current()
            # Without "output", the focused monitor (the one switched last) goes back.
            msg("workspace_back")
            assert current() == {"HEADLESS-1": 3, "HEADLESS-2": 2}, current()

            # Activating a window from the taskbar switches its monitor, and counts as well.
            window = subprocess.Popen([probe, "--external-control"], env=env,
                                      stdout=subprocess.DEVNULL)
            processes.append(window)
            wait_for(lambda: "shaodesk-probe" in windows(), processes, "window mapped")
            _, home, _ = windows()["shaodesk-probe"]
            msg("move_to_workspace", "4")
            assert on(home, "workspace", "1") == 1
            assert windows()["shaodesk-probe"] == (4, home, False), windows()
            subprocess.run([probe, "--activate", "shaodesk-probe"], env=env, check=True,
                           timeout=30, stdout=subprocess.DEVNULL)
            wait_for(lambda: current()[home] == 4, processes, "taskbar switch")
            assert on(home, "workspace_back") == 1
            assert on(home, "workspace_back") == 4

            # With the feature, `workspace N` for the shown workspace goes back instead.
            config.write_text(CONFIG % "true")
            msg("reload")
            assert on("HEADLESS-1", "workspace", "2") == 2
            assert on("HEADLESS-1", "workspace", "2") == 3
            assert on("HEADLESS-1", "workspace", "3") == 2
            assert on("HEADLESS-1", "workspace", "1") == 1
            assert on("HEADLESS-1", "workspace", "1") == 2
            # An unknown feature is an error: the default configuration, without the feature,
            # stands in until it is fixed.
            config.write_text(CONFIG.replace("workspace_back_and_forth", "bogus") % "true")
            msg("reload")
            assert "unknown feature 'bogus'" in log.read_text()
            assert on("HEADLESS-1", "workspace", "2") == 2

            window.kill()
            window.wait(timeout=30)
            processes.remove(window)
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("workspace_back and workspace back-and-forth passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
