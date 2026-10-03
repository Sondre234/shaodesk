# SPDX-License-Identifier: GPL-3.0-or-later
"""The scrolling layout: windows open in columns right of the focused one, the view follows
focus, columns change width, stack and unstack, and the layout survives a change of layout."""
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
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "edge", width = 0.5, presets = { 0.5, 1.0 } } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""
SCREEN_WIDTH = 1280

with tempfile.TemporaryDirectory(prefix="shaode-scroll-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """(focused, tiled, x, y, width, height) per window, left to right, then top down."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return sorted((r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows)

    def by_position():
        return sorted(windows(), key=lambda w: (w[2], w[3]))

    def focused():
        found = [w for w in windows() if w[0]]
        assert len(found) == 1, windows()
        return found[0][2:]

    def visible(rect):
        return rect[0] >= 0 and rect[0] + rect[2] <= SCREEN_WIDTH

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def act(action, predicate, message):
        msg(action)
        wait_for(predicate, message)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            for count in (1, 2, 3):
                processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
            assert msg("get", "layout").split()[0] == "scroll", msg("get", "layout")

            # Three half-width columns on a strip; the newest is focused and in view.
            wait_for(lambda: {w[4] for w in windows()} == {SCREEN_WIDTH // 2}
                     and len({w[2] for w in windows()}) == 3, "three half columns")
            full_height = windows()[0][5]
            assert 0 < full_height <= 720, full_height

            def columns():
                return [w[2] for w in by_position()]

            # Edge follow scrolled just far enough to show the newest column at the right.
            assert columns() == [-640, 0, 640] and focused()[0] == 640, windows()

            # scroll_left focuses the column to the left; the view stays while that column is
            # on screen, and scrolls back once the first column is reached.
            act("scroll_left", lambda: focused()[0] == 0 and columns() == [-640, 0, 640],
                "scroll_left")
            act("scroll_left", lambda: focused()[0] == 0 and columns() == [0, 640, 1280],
                "scroll_left to the first column")
            # Nothing further left.
            msg("scroll_left")
            assert focused()[0] == 0 and columns() == [0, 640, 1280], windows()
            act("scroll_right", lambda: focused()[0] == 640 and columns() == [0, 640, 1280],
                "scroll_right")
            middle = focused()

            # Cycling the width goes to the next preset, wrapping.
            act("column_cycle_width", lambda: focused()[2] == SCREEN_WIDTH,
                "column_cycle_width to full width")
            assert visible(focused()), focused()
            act("column_cycle_width", lambda: focused()[2] == SCREEN_WIDTH // 2,
                "column_cycle_width wraps")
            act("column_widen", lambda: focused()[2] > SCREEN_WIDTH // 2, "column_widen")
            act("column_narrow", lambda: focused()[2] == SCREEN_WIDTH // 2, "column_narrow")

            # center_column puts the focused column in the middle of the screen.
            act("center_column", lambda: focused()[0] == (SCREEN_WIDTH - focused()[2]) // 2,
                "center_column")

            # consume_left stacks the focused window under the one on its left, and the
            # column of windows shares the height; expel puts it back in a column.
            act("consume_left", lambda: len({w[2] for w in windows()}) == 2, "consume_left")
            column = [w for w in windows() if w[2] == focused()[0]]
            assert len(column) == 2 and column[0][5] == column[1][5], windows()
            assert column[0][5] * 2 <= full_height and column[0][4] == column[1][4], windows()
            act("expel", lambda: len({w[2] for w in windows()}) == 3
                and all(w[5] == full_height for w in windows()), "expel")

            # Switching the layout away and back keeps a column per window.
            msg("layout_dwindle")
            wait_for(lambda: msg("get", "layout").split()[0] == "dwindle", "layout_dwindle")
            wait_for(lambda: {w[4] for w in windows()} != {SCREEN_WIDTH // 2}
                     or len({w[2] for w in windows()}) != 3, "dwindle arranged")
            msg("layout_scroll")
            wait_for(lambda: msg("get", "layout").split()[0] == "scroll", "layout_scroll")
            wait_for(lambda: len({w[2] for w in windows()}) == 3, "columns again")

            # Closing every window leaves an empty workspace, and a new one starts over.
            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            wait_for(lambda: not windows(), "windows closed")
            processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                              stdout=subprocess.DEVNULL))
            wait_for(lambda: len(windows()) == 1 and windows()[0][1], "window after closing")
            assert visible(focused()), focused()

            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Scroll layout passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
