# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.drag_strip: dragging the top of a server-decorated window moves it, within the strip
the setting gives (6 pixels by default), and lower down the press reaches the application.
Driven by a virtual pointer (pointer_probe)."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)


def config(strip=None):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false }, %s },
}""" % ("drag_strip = %d," % strip if strip is not None else "")


with tempfile.TemporaryDirectory(prefix="shaodesk-drag-strip-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    log = root / "compositor.log"
    init.write_text(config())
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def where():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: (int(r[4]), int(r[5])) for r in rows}["W"]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: msg("get", "windows"))

    def pointer(*commands):
        virtual.stdin.write(" ".join(commands) + "\n")
        virtual.stdin.flush()
        assert virtual.stdout.readline().strip() == "done"

    def drag_from(depth):
        """Drags 100 pixels right from `depth` pixels below W's top; returns how far it went."""
        x, y = where()
        pointer("move", str(x + 100), str(y + depth), "press", "left",
                "move", str(x + 150), str(y + depth), "move", str(x + 200), str(y + depth),
                "release", "left")
        return where()[0] - x

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            virtual = subprocess.Popen([pointer_probe, str(SCREEN[0]), str(SCREEN[1])], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(virtual)
            assert virtual.stdout.readline().strip() == "ready"
            client = subprocess.Popen([probe, "--window-only"], env=dict(
                env, SHAODESK_PROBE_TITLE="W", SHAODESK_PROBE_SSD="1"), stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: "W" in msg("get", "windows"), "window mapped")

            # The default strip: its top 6 pixels move it, 20 pixels down does not.
            assert drag_from(3) == 100, where()
            assert drag_from(20) == 0, where()

            # A deeper strip catches the press 20 pixels down.
            init.write_text(config(24))
            msg("reload")
            assert drag_from(20) == 100, where()
            assert drag_from(30) == 0, where()

            # 0 turns it off.
            init.write_text(config(0))
            msg("reload")
            assert drag_from(2) == 0, where()

            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("The drag strip moves windows by their top, as deep as windows.drag_strip says")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
