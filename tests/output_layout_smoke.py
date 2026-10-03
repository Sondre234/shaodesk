# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.outputs gives each output its own layout defaults: they apply to workspaces not set by
hand, reach windows already open when the configuration is reloaded, and never override a
layout chosen with an action."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(first=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 0, outputs = {{ {first} }} }},
    outputs = {{
        primary = "HEADLESS-1",
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720" }},
            ["HEADLESS-2"] = {{ mode = "1280x720" }},
        }},
    }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaode-output-layout-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(config())
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def layout(output, workspace=None):
        """(layout, ratio, master count) of a workspace; the current one by default."""
        words = ["get", "layout", output] + ([str(workspace)] if workspace else [])
        name, ratio, count = msg(*words).split()
        return name, float(ratio), int(count)

    def widths():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return sorted(int(r[6]) for r in rows)

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"widths: {widths()}")

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        init.write_text(text)
        server.send_signal(signal.SIGHUP)
        wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads, "reload")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            assert layout("HEADLESS-1") == ("dwindle", 0.55, 1)
            for count in (1, 2):
                processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: len(widths()) == count, f"window {count} mapped")
            assert widths() == [640, 640], widths()

            # A reload reaches workspaces that are already arranged and hold windows.
            reload(config("['HEADLESS-1'] = { tile_layout = 'master', master_ratio = 0.6 }"))
            assert layout("HEADLESS-1") == ("master", 0.6, 1)
            assert layout("HEADLESS-1", 3) == ("master", 0.6, 1)
            assert layout("HEADLESS-2") == ("dwindle", 0.55, 1)
            wait_for(lambda: widths() == [512, 768], "master ratio applied to open windows")

            # An action's choice stays; the other workspaces follow the next reload.
            msg("output", "HEADLESS-1", "layout_monocle")
            assert layout("HEADLESS-1")[0] == "monocle"
            reload(config("['HEADLESS-1'] = { tile_layout = 'spiral', master_count = 2 },"
                          "['HEADLESS-2'] = { master_ratio = 0.7 }"))
            assert layout("HEADLESS-1") == ("monocle", 0.55, 2), layout("HEADLESS-1")
            assert layout("HEADLESS-1", 2) == ("spiral", 0.55, 2)
            assert layout("HEADLESS-2") == ("dwindle", 0.7, 1)

            # Removing an entry returns its workspaces to the global defaults.
            reload(config())
            assert layout("HEADLESS-1", 2) == ("dwindle", 0.55, 1)
            assert layout("HEADLESS-1")[0] == "monocle"
            assert layout("HEADLESS-2") == ("dwindle", 0.55, 1)
            wait_for(lambda: widths() == [1280, 1280], "monocle fills the output")

            bad = subprocess.run([compositor, "msg", "get", "layout", "NOPE"], env=env,
                                 capture_output=True, text=True, timeout=30)
            assert bad.returncode != 0 or "error" in bad.stdout + bad.stderr

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Per-output layout defaults, reloads, and manual choices passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
