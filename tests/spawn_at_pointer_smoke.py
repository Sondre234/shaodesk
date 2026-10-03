# SPDX-License-Identifier: GPL-3.0-or-later
"""A window spawned by a button binding opens centered on the click, kept on screen; one spawned
otherwise, or a later one, is placed as usual. Driven by a virtual pointer (pointer_probe)."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)

with tempfile.TemporaryDirectory(prefix="shaodesk-spawn-at-pointer-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    log = root / "compositor.log"
    init.write_text("""return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = {
        { button = "middle", desktop = true, action = "spawn",
          command = { "env", "SHAODESK_PROBE_TITLE=S", "%s", "--window-only" } },
    },
}""" % probe)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """By title: x, y, width, height."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: tuple(int(n) for n in r[4:8]) for r in rows}

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def pointer(*commands):
        virtual.stdin.write(" ".join(commands) + "\n")
        virtual.stdin.flush()
        assert virtual.stdout.readline().strip() == "done"

    def spawned_at(x, y):
        """Middle-clicks the desktop at (x, y) and returns where S opened, then closes it."""
        pointer("move", str(x), str(y), "click", "middle")
        wait_for(lambda: "S" in windows(), "spawned window mapped")
        place = windows()["S"]
        msg("close")
        wait_for(lambda: "S" not in windows(), "spawned window closed")
        return place

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

            # Centered on the click.
            assert spawned_at(900, 400) == (740, 280, 320, 240), windows()
            # Near a corner it stays on screen.
            assert spawned_at(1270, 710) == (960, 480, 320, 240), windows()
            assert spawned_at(5, 5) == (0, 0, 320, 240), windows()

            # A window opened otherwise cascades as usual, wherever the pointer is.
            pointer("move", "900", "400")
            other = subprocess.Popen([probe, "--window-only"],
                                     env=dict(env, SHAODESK_PROBE_TITLE="O"),
                                     stdout=subprocess.DEVNULL)
            processes.append(other)
            wait_for(lambda: "O" in windows(), "other window mapped")
            assert windows()["O"][:2] != (740, 280), windows()

            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Windows spawned by a button binding open at the pointer")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
