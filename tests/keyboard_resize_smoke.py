# SPDX-License-Identifier: GPL-3.0-or-later
"""resize_left/right/up/down: tiles move the split beside them, floating windows their right or
bottom edge within the output, maximized and fullscreen windows stay put, and
features.keyboard_resize = false turns the actions off."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

GAP = 8
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = %d },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { keyboard_resize = %s },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-resize-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG % (GAP, "true"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def run(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30)

    def msg(*words):
        result = run(*words)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """(focused, tiled, x, y, width, height) per window, most recently focused last."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return [(r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def focused():
        return next(w for w in windows() if w[0])

    def tiles():
        """The left and right tile, by place."""
        return sorted((w for w in windows() if w[1]), key=lambda w: w[2])

    def split():
        """Where the left tile ends: the split between the tiles, less half the gap."""
        left = tiles()[0]
        return left[2] + left[4]

    def near(value, expected, slack=2):
        return abs(value - expected) <= slack

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            for count in (1, 2):
                processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
            wait_for(lambda: len(tiles()) == 2 and tiles()[0][3] == tiles()[1][3],
                     "two tiles side by side")

            # The arrows move the split between the two tiles that way, whichever is focused:
            # the left tile grows toward its right edge, the right one shrinks from its left.
            start = split()
            msg("resize_right", "80")
            wait_for(lambda: near(split(), start + 80), "split moved 80 pixels right")
            msg("resize_left")
            wait_for(lambda: near(split(), start + 40), "split moved back the default 40")
            left, right = tiles()
            assert near(left[2] + left[4] + GAP, right[2]) and near(right[2] + right[4],
                                                                   1280 - GAP), tiles()
            # With no split across the other axis, up and down change nothing.
            before = tiles()
            msg("resize_up")
            msg("resize_down", "100")
            assert tiles() == before, (before, tiles())
            # Bad sizes are refused.
            for words in (("resize_right", "x"), ("resize_right", "0"),
                          ("resize_right", "40", "40"), ("resize_right", "99999")):
                assert run(*words).returncode != 0, words
            # The split stops short of the output's edge.
            msg("resize_right", "4000")
            wait_for(lambda: near(split(), 1280 * 0.9 - GAP // 2),
                     "split limited to 90%")

            # Floating: the right and bottom edges move that way, inside the output.
            msg("toggle_floating")
            wait_for(lambda: not focused()[1], "focused window floating")
            _, _, x, y, width, height = focused()
            msg("resize_right", "50")
            msg("resize_down", "30")
            wait_for(lambda: focused()[2:] == (x, y, width + 50, height + 30),
                     "floating window grew right and down")
            msg("resize_left", "20")
            msg("resize_up", "10")
            wait_for(lambda: focused()[2:] == (x, y, width + 30, height + 20),
                     "floating window shrank from the right and bottom")
            msg("resize_right", "4000")
            msg("resize_down", "4000")
            wait_for(lambda: focused()[2] + focused()[4] == 1280 - GAP and
                     focused()[2:4] == (x, y), "floating window stopped at the output's edge")
            bottom = focused()[3] + focused()[5]
            assert bottom <= 720 - GAP, focused()
            msg("resize_left", "4000")
            msg("resize_up", "4000")
            wait_for(lambda: focused()[2:] == (x, y, 64, 64), "floating window kept a minimum")
            # Sticky windows float, and resize as such.
            msg("toggle_sticky")
            msg("resize_right", "36")
            wait_for(lambda: focused()[2:] == (x, y, 100, 64), "sticky window resized")
            msg("toggle_sticky")
            wait_for(lambda: not focused()[1], "unstuck window still floating")

            # Maximized and fullscreen windows stay as they are.
            msg("maximize")
            wait_for(lambda: focused()[4] > 1000, "maximized")
            before = focused()
            for action in ("resize_left", "resize_right", "resize_up", "resize_down"):
                msg(action, "60")
            assert focused() == before, (before, focused())
            msg("restore")
            wait_for(lambda: focused()[4] < 1000, "restored")
            msg("fullscreen")
            wait_for(lambda: focused()[4] == 1280, "fullscreen")
            before = focused()
            msg("resize_left", "60")
            assert focused() == before, (before, focused())
            msg("fullscreen")
            wait_for(lambda: focused()[4] < 1280, "left fullscreen")

            # Turned off, the actions still parse but do nothing, for tiles and floating windows.
            init.write_text(CONFIG % (GAP, "false"))
            msg("reload")
            before = focused()
            msg("resize_right", "80")
            assert focused() == before, (before, focused())
            msg("toggle_floating")
            wait_for(lambda: len(tiles()) == 2, "tiled again")
            start = split()
            msg("resize_right", "80")
            msg("resize_left", "80")
            assert split() == start, (start, tiles())
            # Turned back on, the same request works again.
            init.write_text(CONFIG % (GAP, "true"))
            msg("reload")
            msg("resize_left", "80")
            wait_for(lambda: near(split(), start - 80), "resizing back on")

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=30)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Keyboard resizing of tiles and floating windows, and its toggle, passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
