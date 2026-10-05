# SPDX-License-Identifier: GPL-3.0-or-later
"""Hot corners run their request after the pointer has rested in the corner, once per visit;
a pass through does nothing. The pointer is moved with wlrctl, a virtual pointer."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import harness

compositor = str(Path(sys.argv[1]).resolve())
wlrctl = shutil.which("wlrctl")
if not wlrctl:
    print("wlrctl not found: hot corners not driven")
    sys.exit(0)

with tempfile.TemporaryDirectory(prefix="shaodesk-corner-test-") as directory:
    root = Path(directory)
    flag = root / "spawned"
    config = root / "init.lua"
    config.write_text(f"""return {{
    xwayland = false,
    layout = {{ tiling = true, workspaces = 4 }},
    hot_corners = {{
        size = 10, delay = 800,
        top_left = "workspace 2",
        top_right = "workspace 3",
        bottom_left = "spawn touch {flag}",
    }},
}}""")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout

    def workspace():
        return int(msg("get", "workspace").strip())

    def move(dx, dy):
        subprocess.run([wlrctl, "pointer", "move", str(dx), str(dy)], env=env, check=True,
                       timeout=30)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)], env=env,
                                  stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert workspace() == 1

            def wait_for(predicate, message):
                harness.wait_for(predicate, processes, message,
                                 detail=lambda: f"workspace {workspace()}")

            # Rest in the top-left corner: nothing at first, then the request runs.
            move(-5000, -5000)
            time.sleep(0.1)
            assert workspace() == 1, "ran before the delay"
            wait_for(lambda: workspace() == 2, "top-left corner")

            # Staying, even moving inside the corner, does not run it again.
            msg("workspace", "1")
            move(3, 3)
            time.sleep(0.2)
            move(-3, -3)
            time.sleep(1.1)
            assert workspace() == 1, "ran twice in one visit"

            # A pass through the corner without resting does nothing.
            move(400, 300)
            move(-5000, -5000)
            time.sleep(0.1)
            move(400, 300)
            time.sleep(1.1)
            assert workspace() == 1, "ran without the delay"

            # Coming back runs it again; the top-right corner runs its own request.
            move(-5000, -5000)
            wait_for(lambda: workspace() == 2, "top-left corner again")
            move(5000, 0)
            wait_for(lambda: workspace() == 3, "top-right corner")

            # A corner with a spawn request starts the program.
            assert not flag.exists()
            move(-5000, 5000)
            wait_for(flag.exists, "spawn from the bottom-left corner")

            # A corner with nothing bound does nothing.
            msg("workspace", "1")
            move(5000, 5000)
            time.sleep(1.1)
            assert workspace() == 1

            # Reloading without corners turns them off.
            config.write_text("return { xwayland = false, layout = { workspaces = 4 } }")
            msg("reload")
            move(-5000, -5000)
            move(400, 300)
            move(-5000, -5000)
            time.sleep(1.1)
            assert workspace() == 1, "ran after the reload removed it"

            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Hot corners run after a rest, once per visit")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
