# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiling layouts: layout_next and layout_<name> switch a workspace between dwindle,
master-stack, spiral and monocle; master_grow, master_more and promote work in master-stack."""
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
    layout = { tiling = true, master_ratio = 0.6 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-layout-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
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
        """(focused, tiled, x, y, width, height) per window."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return [(r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows]

    def rects():
        return sorted(w[2:] for w in windows())

    def layout():
        return msg("get", "layout").split()

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"windows: {windows()}, layout: {layout()}")

    def disjoint(items):
        return all(a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or
                   a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1]
                   for i, a in enumerate(items) for b in items[i + 1:])

    def in_layout(name):
        msg(f"layout_{name}")
        wait_for(lambda: layout()[0] == name, f"layout {name}")

    def master_column():
        """The tiles at the left edge, and the others."""
        items = rects()
        left = min(r[0] for r in items)
        return [r for r in items if r[0] == left], [r for r in items if r[0] != left]

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            for count in (1, 2, 3):
                processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
            assert layout() == ["dwindle", "0.60", "1"], layout()

            # Master-stack: one master on the left, the others stacked in the right column.
            in_layout("master")
            wait_for(lambda: len(master_column()[0]) == 1 and len(master_column()[1]) == 2,
                     "master and stack")
            (master,), stack = master_column()
            assert master[2] > stack[0][2] and stack[0][2] == stack[1][2], (master, stack)
            assert stack[0][0] == stack[1][0] and disjoint(rects()), rects()
            assert abs(master[3] - (stack[0][3] + stack[1][3])) < 40, (master, stack)

            msg("master_grow")
            wait_for(lambda: master_column()[0][0][2] > master[2], "master_grow")
            msg("master_shrink")
            wait_for(lambda: master_column()[0][0][2] == master[2], "master_shrink")
            msg("master_more")
            wait_for(lambda: len(master_column()[0]) == 2 and len(master_column()[1]) == 1,
                     "master_more")
            assert layout()[2] == "2", layout()
            msg("master_less")
            wait_for(lambda: len(master_column()[0]) == 1, "master_less")

            # promote gives the focused tile, moved into the stack first, the master column.
            if next(w[2:] for w in windows() if w[0]) == master:
                msg("focus_next")
                wait_for(lambda: next(w[2:] for w in windows() if w[0]) != master, "focus_next")
            msg("promote")
            wait_for(lambda: next(w[2:] for w in windows() if w[0]) == master_column()[0][0],
                     "promote")

            # Spiral: disjoint tiles, the first taking the left of the area.
            in_layout("spiral")
            wait_for(lambda: len(master_column()[0]) == 1 and disjoint(rects()), "spiral")

            # Monocle: every tile covers the same area, and focus_next steps between them.
            # The tiles look alike there, so the focused one is found in master-stack.
            def slot():
                in_layout("master")
                wait_for(lambda: len(master_column()[0]) == 1 and disjoint(rects()), "master")
                return next(w[2:] for w in windows() if w[0])

            start = slot()
            in_layout("monocle")
            wait_for(lambda: len(set(rects())) == 1, "monocle")
            assert len([w for w in windows() if w[0]]) == 1
            msg("focus_next")
            after = slot()
            assert after != start, "focus_next did not move the focus"
            in_layout("monocle")
            msg("focus_prev")
            assert slot() == start, "focus_prev did not move back"
            in_layout("monocle")

            # Back to dwindle, and layout_next / layout_prev walk the cycle.
            in_layout("dwindle")
            wait_for(lambda: len(set(rects())) == 3 and disjoint(rects()), "dwindle again")
            msg("layout_next")
            wait_for(lambda: layout()[0] == "master", "layout_next")
            msg("layout_prev")
            wait_for(lambda: layout()[0] == "dwindle", "layout_prev")

            # Another workspace keeps its own layout.
            msg("workspace", "2")
            wait_for(lambda: layout()[0] == "dwindle", "second workspace")
            in_layout("monocle")
            msg("workspace", "1")
            wait_for(lambda: layout()[0] == "dwindle", "first workspace kept its layout")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Tiling layouts passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
