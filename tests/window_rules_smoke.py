# SPDX-License-Identifier: GPL-3.0-or-later
"""Window rules float, size, place, and send new windows elsewhere, unless turned off."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def settings(enabled):
    return f"""return {{
    xwayland = false,
    features = {{ window_rules = {str(enabled).lower()} }},
    layout = {{ tiling = true, workspaces = 4 }},
    outputs = {{ order = {{ "HEADLESS-1", "HEADLESS-2" }},
                 monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }},
                               ["HEADLESS-2"] = {{ mode = "1280x720" }} }} }},
    windows = {{ rules = {{
        {{ app_id = "^rule-float$", floating = true, size = {{ 400, 300 }} }},
        {{ app_id = "float$", position = "center" }},
        {{ app_id = "^rule-away$", workspace = 3, output = "HEADLESS-2" }},
        {{ title = "^Pinned", floating = true, size = {{ width = 200, height = 100 }},
           position = {{ 20, 30 }} }},
        {{ app_id = "^rule-quiet$", floating = true, focus = false }},
        {{ app_id = "^rule-max$", maximize = true }},
        {{ app_id = "^rule-full$", fullscreen = true }},
        {{ app_id = "^rule-sticky$", sticky = true, workspace = 2 }},
    }} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaode-rules-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings(True))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30, check=True)
        return result.stdout

    def windows():
        """By title: workspace, focused, tiled, (x, y, width, height), visible, output,
        sticky."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: (int(r[0]), r[1] == "1", r[3] == "1", tuple(map(int, r[4:8])),
                       r[11] == "1", r[10], r[13] == "1") for r in rows}

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def open_window(app_id, title):
        processes.append(subprocess.Popen(
            [probe, "--window-only"], stdout=subprocess.DEVNULL,
            env=dict(env, SHAODE_PROBE_APP_ID=app_id, SHAODE_PROBE_TITLE=title)))
        wait_for(lambda: title in windows(), f"{title} opens")

    def window(title):
        return windows().get(title)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            first = next(line for line in msg("get", "outputs").splitlines()
                         if line.startswith("HEADLESS-1\t"))
            _, _, x, y, width, height, *_ = first.split("\t")
            assert (int(x), int(y), int(width), int(height)) == (0, 0, 1280, 720), first
            width, height = 1280, 720

            open_window("shaode-probe", "Plain")
            wait_for(lambda: window("Plain")[:3] == (1, True, True) and
                     window("Plain")[5] == "HEADLESS-1", "plain window tiles")

            # Two rules merge: floating and size from one, centred by the next.
            open_window("rule-float", "Floating")
            center = ((width - 400) // 2, (height - 300) // 2, 400, 300)
            wait_for(lambda: window("Floating")[:6] == (1, True, False, center, True, "HEADLESS-1"),
                     "floating, sized, and centred")

            # Opens tiled on workspace 3 of the other output without switching there or taking
            # focus.
            open_window("rule-away", "Away")
            wait_for(lambda: window("Away")[:3] == (3, False, True) and not window("Away")[4] and
                     window("Away")[5] == "HEADLESS-2" and window("Away")[3][0] >= width,
                     "sent to workspace 3 of HEADLESS-2")
            assert msg("get", "workspace") == "1\n"
            assert window("Floating")[1], "a window on another workspace took focus"

            open_window("shaode-probe", "Pinned window")
            wait_for(lambda: window("Pinned window")[:5] == (1, True, False, (20, 30, 200, 100),
                                                             True),
                     "title rule places the window at 20, 30")

            open_window("rule-quiet", "Quiet")
            wait_for(lambda: not window("Quiet")[2] and window("Quiet")[4],
                     "quiet window floats in view")
            assert window("Pinned window")[1] and not window("Quiet")[1], windows()

            # Maximized floats over the tiles; fullscreen covers the output.
            open_window("rule-max", "Maximized")
            wait_for(lambda: window("Maximized")[:4] == (1, True, False, (0, 0, width, height)),
                     "maximized by a rule")
            open_window("rule-full", "Fullscreen")
            wait_for(lambda: window("Fullscreen")[:4] == (1, True, True, (0, 0, width, height)),
                     "fullscreen by a rule")
            msg("fullscreen")
            wait_for(lambda: window("Fullscreen")[3] != (0, 0, width, height),
                     "fullscreen from a rule returns to its tile")

            # Sticky floats on the current workspace and stays shown on the others.
            open_window("rule-sticky", "Sticky")
            wait_for(lambda: window("Sticky")[:3] == (1, True, False) and window("Sticky")[6],
                     "sticky by a rule")
            msg("workspace", "2")
            assert window("Sticky")[4] and not window("Plain")[4], windows()
            msg("workspace", "1")

            # Turned off, the same rules leave new windows to the defaults.
            config.write_text(settings(False))
            server.send_signal(signal.SIGHUP)
            wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
            open_window("rule-float", "Floating again")
            wait_for(lambda: window("Floating again")[:3] == (1, True, True),
                     "rule actions are off: the window tiles")
            open_window("rule-away", "Away again")
            wait_for(lambda: window("Away again")[:3] == (1, True, True) and
                     window("Away again")[5] == "HEADLESS-1",
                     "rule actions are off: the window opens on workspace 1 here")

            for process in processes[1:]:
                process.terminate()
                process.wait(timeout=30)
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Window rule actions and features.window_rules passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
