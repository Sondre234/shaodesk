# SPDX-License-Identifier: GPL-3.0-or-later
"""Window groups in awkward situations: a group moved to another workspace, tiling switched off
and on, fullscreen, floating groups, the scroll layout, and closing hidden members."""
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
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { groups = %s },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-group-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG % "true")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def rows():
        """By title: the workspace, then the columns of `get windows` from x on."""
        return {r[9]: (int(r[0]),) for r in
                (line.split("\t") for line in msg("get", "windows").splitlines())}

    def windows():
        """By title: focused, tiled, x, y, width, height, visible, group."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(focused=r[1] == "1", tiled=r[3] == "1", x=int(r[4]), y=int(r[5]),
                           width=int(r[6]), height=int(r[7]), visible=r[11] == "1",
                           group=int(r[14]))
                for r in rows}

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def rect(name):
        w = windows()[name]
        return (w["x"], w["y"], w["width"], w["height"])

    def focused():
        return [t for t, w in windows().items() if w["focused"]]

    clients = {}

    def open_window(title):
        clients[title] = subprocess.Popen([probe, "--window-only"],
                                          env=dict(env, SHAODESK_PROBE_TITLE=title),
                                          stdout=subprocess.DEVNULL)
        processes.append(clients[title])
        wait_for(lambda: title in windows() and windows()[title]["focused"], f"{title} mapped")

    def close_window(title):
        clients[title].kill()
        clients[title].wait(timeout=30)
        processes.remove(clients[title])
        wait_for(lambda: title not in windows(), f"{title} closed")

    def settled(*names):
        """The windows are visible and tiled, and their rectangles no longer change."""
        def check():
            first = [rect(n) for n in names]
            return all(windows()[n]["tiled"] and windows()[n]["visible"] for n in names) and \
                all(r[2] > 0 for r in first)
        wait_for(check, f"{names} tiled")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            open_window("A")
            open_window("B")
            settled("A", "B")
            msg("group_toggle")
            open_window("C")
            wait_for(lambda: windows()["C"]["group"] == windows()["B"]["group"] != 0, "C joined")
            group = windows()["B"]["group"]

            # Moving the shown member to another workspace takes the whole group along: the
            # hidden member is on that workspace too and comes forward there.
            msg("move_to_workspace", "2")
            wait_for(lambda: not windows()["C"]["visible"] and windows()["A"]["visible"], "C left")
            msg("workspace", "2")
            wait_for(lambda: windows()["C"]["visible"] and not windows()["B"]["visible"],
                     "C on workspace 2")
            msg("group_prev")
            wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"] and
                     windows()["B"]["tiled"], "B forward on workspace 2")
            assert rows()["B"][0] == 2 and rows()["C"][0] == 2, rows()
            msg("workspace", "1")
            wait_for(lambda: not windows()["B"]["visible"] and windows()["A"]["visible"],
                     "back on workspace 1")

            # Tiling switched off floats the shown member; the hidden one waits, and comes
            # back into the tiling with it.
            msg("workspace", "2")
            wait_for(lambda: windows()["B"]["visible"], "on workspace 2 again")
            msg("toggle_tiling")
            wait_for(lambda: not windows()["B"]["tiled"] and windows()["B"]["visible"], "floated")
            assert not windows()["C"]["visible"] and windows()["C"]["group"] == group
            msg("group_next")
            wait_for(lambda: focused() == ["C"] and windows()["C"]["visible"] and
                     not windows()["B"]["visible"], "C shown while floating")
            assert not windows()["C"]["tiled"], windows()
            msg("toggle_tiling")
            wait_for(lambda: windows()["C"]["tiled"] and not windows()["B"]["tiled"] and
                     not windows()["B"]["visible"], "C tiled again, B still hidden")

            # Fullscreen: switching tabs leaves it rather than hiding a fullscreen window.
            msg("fullscreen")
            wait_for(lambda: rect("C")[2] >= 1270, "C fullscreen")
            msg("group_next")
            wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"] and
                     not windows()["C"]["visible"], "B shown")
            wait_for(lambda: rect("B")[2] < 1270, "B not fullscreen")
            msg("group_next")
            wait_for(lambda: focused() == ["C"] and windows()["C"]["visible"], "C shown again")
            wait_for(lambda: rect("C")[2] < 1270 and windows()["C"]["tiled"], "C left fullscreen")
            msg("group_next")
            wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"], "B shown once more")

            # Closing a hidden member leaves the shown one alone, and the group of one goes.
            close_window("C")
            wait_for(lambda: windows()["B"]["group"] == 0 and windows()["B"]["visible"] and
                     windows()["B"]["tiled"], "B alone")

            # The scrolling layout: a group is one column slot.
            msg("layout_scroll")
            msg("group_toggle")
            open_window("E")
            wait_for(lambda: windows()["E"]["group"] == windows()["B"]["group"] != 0,
                     "E joined B in the scroll layout")
            wait_for(lambda: not windows()["B"]["visible"] and windows()["E"]["tiled"], "E shown")
            columns = rect("E")
            msg("group_prev")
            wait_for(lambda: focused() == ["B"] and rect("B") == columns, "B in E's column")
            close_window("B")
            wait_for(lambda: windows()["E"]["visible"] and windows()["E"]["group"] == 0, "E left")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Groups followed workspaces, tiling, fullscreen, the scroll layout, and closing")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
