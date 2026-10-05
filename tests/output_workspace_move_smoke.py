# SPDX-License-Identifier: GPL-3.0-or-later
"""move_workspace_to_output sends a workspace, with its windows and layout, to another output;
swap_workspaces trades all workspaces of two outputs and what they show."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(primary="HEADLESS-1", second_tiling="true"):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 0 }},
    animations = {{ duration = 1000 }},
    outputs = {{
        primary = "{primary}",
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720" }},
            ["HEADLESS-2"] = {{ mode = "1280x720", tiling = {second_tiling} }},
        }},
    }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaodesk-workspace-move-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(config())
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def run(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=5)

    def msg(*words):
        result = run(*words)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def outputs():
        rows = [line.split("\t") for line in msg("get", "outputs").splitlines()]
        return {row[0]: tuple(int(v) for v in row[2:6]) for row in rows}

    def windows():
        """title -> (workspace, tiled, x, y, width, height, output, visible)."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: (int(r[0]), r[3] == "1", *map(int, r[4:8]), r[10], r[11] == "1")
                for r in rows}

    def shown():
        """The workspace each output shows."""
        rows = [line.split("\t") for line in msg("get", "workspaces").splitlines()]
        return {row[0]: int(row[1]) for row in rows}

    def layout(output, workspace):
        name, ratio, count = msg("get", "layout", output, str(workspace)).split()
        return name, float(ratio), int(count)

    def place(title):
        w = windows()[title]
        return w[0], w[6]

    def inside(title):
        w = windows()[title]
        x, y, width, height = outputs()[w[6]]
        return x <= w[2] and y <= w[3] and w[2] + w[4] <= x + width and w[3] + w[5] <= y + height

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"windows: {windows()}, shown: {shown()}")

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        init.write_text(text)
        server.send_signal(signal.SIGHUP)
        wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads, "reload")

    def launch(title):
        processes.append(subprocess.Popen(
            [probe, "--window-only"],
            env=dict(env, SHAODESK_PROBE_TITLE=title, SHAODESK_PROBE_APP_ID=f"app-{title.lower()}"),
            stdout=subprocess.DEVNULL))
        wait_for(lambda: title in windows(), f"{title} mapped")

    def running():
        return int(msg("get", "animations").split()[0])

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            # HEADLESS-1: A and B on workspace 1 in the master layout, C on workspace 2.
            # HEADLESS-2: D on workspace 1.
            launch("A")
            launch("B")
            msg("output", "HEADLESS-1", "layout_master")
            msg("output", "HEADLESS-1", "master_grow")
            msg("output", "HEADLESS-1", "workspace", "2")
            launch("C")
            msg("output", "HEADLESS-1", "workspace", "1")
            reload(config(primary="HEADLESS-2"))
            launch("D")
            wait_for(lambda: all(w[1] for w in windows().values()), "all tiled")
            ratio = layout("HEADLESS-1", 1)
            assert ratio[0] == "master" and ratio[1] > 0.55, ratio
            assert [place(t) for t in "ABCD"] == [(1, "HEADLESS-1"), (1, "HEADLESS-1"),
                                                  (2, "HEADLESS-1"), (1, "HEADLESS-2")]

            # A target that is not there, or no target, changes nothing.
            msg("output", "HEADLESS-1", "move_workspace_to_output", "right")  # H1 is rightmost
            msg("output", "HEADLESS-1", "move_workspace_to_output", "HEADLESS-9")
            assert [place(t) for t in "ABCD"] == [(1, "HEADLESS-1"), (1, "HEADLESS-1"),
                                                  (2, "HEADLESS-1"), (1, "HEADLESS-2")]
            assert run("move_workspace_to_output").returncode != 0
            assert run("move_workspace_to_output", "left", "right").returncode == 0  # a description

            # Move workspace 1 of HEADLESS-1 to the output on its left; D comes the other way.
            msg("output", "HEADLESS-1", "move_workspace_to_output", "left")
            assert running() > 0, "the windows do not glide"
            wait_for(lambda: place("A") == (1, "HEADLESS-2") and place("D") == (1, "HEADLESS-1"),
                     "workspace moved")
            assert place("B") == (1, "HEADLESS-2") and place("C") == (2, "HEADLESS-1")
            wait_for(lambda: all(inside(t) for t in "ABCD"), "windows inside their outputs")
            moved = layout("HEADLESS-2", 1)
            assert moved == ratio, (moved, ratio)
            assert layout("HEADLESS-1", 1)[0] == "dwindle"
            state = windows()
            assert state["A"][1] and state["B"][1] and state["D"][1]
            assert state["A"][4] == round(1280 * ratio[1]) and state["B"][4] == 1280 - state["A"][4], \
                state
            assert state["D"][4] == 1280, state
            assert shown() == {"HEADLESS-1": 1, "HEADLESS-2": 1}, shown()

            # And back, by name.
            msg("output", "HEADLESS-2", "move_workspace_to_output", "HEADLESS-1")
            wait_for(lambda: place("A") == (1, "HEADLESS-1") and place("D") == (1, "HEADLESS-2"),
                     "workspace moved back")
            assert layout("HEADLESS-1", 1) == ratio and layout("HEADLESS-2", 1)[0] == "dwindle"

            # Swapping trades everything, and the workspaces on screen.
            msg("output", "HEADLESS-1", "workspace", "2")
            assert shown() == {"HEADLESS-1": 2, "HEADLESS-2": 1}
            msg("output", "HEADLESS-1", "swap_workspaces", "HEADLESS-2")
            wait_for(lambda: place("C") == (2, "HEADLESS-2") and place("A") == (1, "HEADLESS-2")
                     and place("D") == (1, "HEADLESS-1"), "workspaces swapped")
            assert place("B") == (1, "HEADLESS-2")
            assert shown() == {"HEADLESS-1": 1, "HEADLESS-2": 2}, shown()
            assert layout("HEADLESS-2", 1) == ratio
            wait_for(lambda: all(inside(t) for t in "ABCD"), "windows inside their outputs after swap")
            assert windows()["C"][7] and windows()["D"][7] and not windows()["A"][7]
            # With no target it swaps with the next output, wrapping.
            msg("output", "HEADLESS-2", "swap_workspaces")
            wait_for(lambda: place("C") == (2, "HEADLESS-1") and place("D") == (1, "HEADLESS-2"),
                     "swapped back with the default target")
            assert shown() == {"HEADLESS-1": 2, "HEADLESS-2": 1}, shown()

            # An output that does not tile: tiles float there, and its floating windows tile.
            msg("output", "HEADLESS-1", "workspace", "1")
            reload(config(primary="HEADLESS-2", second_tiling="false"))
            wait_for(lambda: not windows()["D"][1], "D floats on HEADLESS-2")
            msg("output", "HEADLESS-1", "move_workspace_to_output", "HEADLESS-2")
            wait_for(lambda: place("A") == (1, "HEADLESS-2") and place("D") == (1, "HEADLESS-1"),
                     "workspace moved to the floating output")
            wait_for(lambda: not windows()["A"][1] and not windows()["B"][1] and windows()["D"][1],
                     "tiles float, floating window tiles")
            wait_for(lambda: all(inside(t) for t in "ABCD"), "windows inside their outputs")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=5)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Workspaces moved and swapped between outputs")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
