# SPDX-License-Identifier: GPL-3.0-or-later
"""Magnetic edges: a floating window dragged near the edge of the output, of the area a panel
leaves free or of another window lands on it and stays held, a guide line shows the edge, the
bypass modifier and windows.magnet turn it off, and dropping at the top still maximizes.
Driven by a virtual pointer and keyboard (pointer_probe); the window asks for the move the way a
client-decorated one does (wayland_probe with SHAODESK_PROBE_MOVE)."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

grim = sys.argv[4] if len(sys.argv) > 4 else ""
SCREEN = (1280, 720)
PANEL = 48


def config(magnet=""):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { %s } },
}""" % magnet


with tempfile.TemporaryDirectory(prefix="shaodesk-magnet-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(config())
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """By title: x, y, width, height, tiled."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: (int(r[4]), int(r[5]), int(r[6]), int(r[7]), r[3] == "1") for r in rows}

    def where():
        return windows()["W"][:2]

    def guides():
        """(shown, x, y, width, height) of the vertical and the horizontal line."""
        rows = [tuple(int(n) for n in line.split("\t")) for line in msg("get", "guides").splitlines()]
        return rows

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def pointer(*commands):
        virtual.stdin.write(" ".join(commands) + "\n")
        virtual.stdin.flush()
        assert virtual.stdout.readline().strip() == "done"

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        virtual = None
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            virtual = subprocess.Popen([pointer_probe, str(SCREEN[0]), str(SCREEN[1])], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(virtual)
            assert virtual.stdout.readline().strip() == "ready"

            # A panel along the bottom (with a window of its own, "P"), and the window to drag.
            panel = subprocess.Popen([probe, "--external-control"], env=dict(
                env, SHAODESK_PROBE_TITLE="P"), stdout=subprocess.DEVNULL)
            processes.append(panel)
            wait_for(lambda: "P" in windows(), "panel client mapped")
            client = subprocess.Popen([probe, "--window-only"], env=dict(
                env, SHAODESK_PROBE_TITLE="W", SHAODESK_PROBE_MOVE="1"), stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: "W" in windows(), "window mapped")
            px, py, pw, ph, _ = windows()["P"]
            ww, wh = windows()["W"][2:4]
            assert (ww, wh) == (320, 240), windows()

            def drag():
                """Presses on W, so it asks to move."""
                wx, wy = where()
                pointer("move", str(wx + 100), str(wy + 100), "press", "left")
                time.sleep(0.15)  # the client turns the press into a move request

            def to(grab_offset, x, y):
                """Moves the pointer so the window would be at (x, y) without magnetism."""
                pointer("move", str(x + grab_offset), str(y + grab_offset))

            def release():
                pointer("release", "left")

            # The output's left edge holds it (5 px away), and it stays 12 px out and no more.
            drag()
            to(100, 5, 200)
            assert where() == (0, 200), where()
            assert guides()[0] == (1, -1, 200, 3, 240) and guides()[1][0] == 0, guides()
            if grim:  # the guide is drawn over the window's edge: those pixels differ from inside
                shot = harness.grab(grim, env)
                assert shot.at(0, 300) != shot.at(8, 300), (shot.at(0, 300), shot.at(8, 300))
            to(100, 11, 200)
            assert where() == (0, 200), where()
            to(100, 13, 200)
            assert where() == (13, 200), where()
            assert guides()[0][0] == 0, guides()
            if grim:
                shot = harness.grab(grim, env)
                assert shot.at(14, 300) == shot.at(22, 300), (shot.at(14, 300), shot.at(22, 300))
            # The right edge, and the panel's edge along the bottom (the window ends at 672).
            to(100, SCREEN[0] - 320 - 5, 200)
            assert where() == (SCREEN[0] - 320, 200), where()
            assert guides()[0] == (1, SCREEN[0] - 1, 200, 3, 240), guides()
            to(100, 600, SCREEN[1] - PANEL - 240 - 6)
            assert where() == (600, SCREEN[1] - PANEL - 240), where()
            assert guides()[1] == (1, 600, SCREEN[1] - PANEL - 1, 320, 3), guides()
            assert guides()[0][0] == 0, guides()
            # Far from anything, it goes where the pointer says.
            to(100, 600, 350)
            assert where() == (600, 350), where()
            assert guides()[0][0] == 0 and guides()[1][0] == 0, guides()

            # Another window's edges: it touches P's right edge, and lines up with its top.
            to(100, px + pw + 7, py + 6)
            assert where() == (px + pw, py), (where(), windows())
            assert guides()[0][0] == 1 and guides()[1][0] == 1, guides()
            # Far below P there is nothing to hold on to (P ends at py + ph).
            to(100, px + pw + 7, py + ph + 100)
            assert where() == (px + pw + 7, py + ph + 100), where()

            # With Shift held the drag is free; without, it lands again.
            pointer("key", "shift", "down")
            to(100, 5, 200)
            assert where() == (5, 200), where()
            assert guides()[0][0] == 0, guides()
            pointer("key", "shift", "up")
            to(100, 6, 200)
            assert where() == (0, 200), where()
            release()
            time.sleep(0.05)
            assert where() == (0, 200) and guides()[0][0] == 0, (where(), guides())

            # Dropping at the top of the screen still maximizes.
            drag()
            pointer("move", "640", "0")
            release()
            wait_for(lambda: windows()["W"][2] > 1000, "dropped at the top: maximized")
            assert windows()["W"][3] > 600 and not windows()["W"][4], windows()
            msg("restore")
            wait_for(lambda: windows()["W"][2] == 320, "restored")

            # A shorter distance, or none, changes what is caught; the guides can be left out.
            init.write_text(config("distance = 4"))
            msg("reload")
            drag()
            to(100, 5, 200)
            assert where() == (5, 200), where()
            to(100, 3, 200)
            assert where() == (0, 200), where()
            release()
            init.write_text(config("distance = 30, guides = false, bypass = \"none\""))
            msg("reload")
            drag()
            to(100, 25, 380)
            assert where() == (0, 380), where()
            assert guides()[0][0] == 0, guides()
            pointer("key", "shift", "down")  # no bypass modifier is set
            to(100, 26, 380)
            assert where() == (0, 380), where()
            pointer("key", "shift", "up")
            release()
            init.write_text(config("enabled = false"))
            msg("reload")
            drag()
            to(100, 3, 200)
            assert where() == (3, 200), where()
            assert guides()[0][0] == 0, guides()
            release()

            # Resizing: the edges being pulled stick to the same lines, the others stay put.
            init.write_text(config())
            msg("reload")
            client.terminate()
            client.wait(timeout=5)
            processes.remove(client)
            wait_for(lambda: "W" not in windows(), "window closed")
            client = subprocess.Popen([probe, "--window-only"], env=dict(
                env, SHAODESK_PROBE_TITLE="W", SHAODESK_PROBE_RESIZE="bottom_right"),
                stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: "W" in windows(), "window mapped")
            wx, wy = where()

            def size():
                return windows()["W"][2:4]

            def resized(width, height):
                wait_for(lambda: size() == (width, height), f"resized to {width}x{height}")

            # Press on the bottom right corner (one pixel inside), and pull it near the corner
            # of the free area: it lands on the output's right edge and above the panel.
            pointer("move", str(wx + 319), str(wy + 239), "press", "left")
            time.sleep(0.15)
            pointer("move", str(SCREEN[0] - 5 - 1), str(SCREEN[1] - PANEL - 6 - 1))
            resized(SCREEN[0] - wx, SCREEN[1] - PANEL - wy)
            assert guides()[0][0] == 1 and guides()[1][0] == 1, guides()
            pointer("move", str(SCREEN[0] - 40 - 1), str(SCREEN[1] - PANEL - 40 - 1))
            resized(SCREEN[0] - wx - 40, SCREEN[1] - PANEL - wy - 40)
            assert guides()[0][0] == 0 and guides()[1][0] == 0, guides()
            pointer("key", "shift", "down")
            pointer("move", str(SCREEN[0] - 5 - 1), str(SCREEN[1] - PANEL - 6 - 1))
            resized(SCREEN[0] - wx - 5, SCREEN[1] - PANEL - wy - 6)
            pointer("key", "shift", "up")
            pointer("release", "left")
            assert windows()["W"][:2] == (wx, wy), windows()  # the corner pulled the far edges only

            client.terminate()
            client.wait(timeout=5)
            processes.remove(client)
            panel.terminate()
            panel.wait(timeout=5)
            processes.remove(panel)
            virtual.stdin.close()
            virtual.wait(timeout=5)
            processes.remove(virtual)
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Magnetic edges hold a dragged window, show guides, and obey their settings"
                  + ("" if grim else " (pixels not checked: grim missing)"))
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
