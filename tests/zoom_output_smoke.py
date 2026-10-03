# SPDX-License-Identifier: GPL-3.0-or-later
"""The magnifier on several outputs: an output it cannot magnify (a rotated one) shows 1x without
switching the magnifier off for the others, and the level survives a return to 1x. The pointer
is moved with wlrctl."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

import harness

compositor = str(Path(sys.argv[1]).resolve())
wlrctl = shutil.which("wlrctl")
if not wlrctl:
    print("wlrctl not found: skipped")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = {
        primary = "HEADLESS-1",
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720", transform = 1 },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
    zoom = { step = 2, max = 4, duration = 0 },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-zoom-output-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout

    def zoom():
        """(level, target, magnified outputs), levels in thousandths"""
        return tuple(int(n) for n in msg("get", "zoom").splitlines()[0].split("\t"))

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
            outputs = {line.split("\t")[0]: line.split("\t")
                       for line in msg("get", "outputs").splitlines()}
            first, second = outputs["HEADLESS-1"], outputs["HEADLESS-2"]
            assert int(first[2]) < int(second[2]), (first, second)  # 1 is to the left of 2

            detail = lambda: f"zoom: {zoom()}\n{msg('get', 'outputs')}"
            # The pointer on the rotated output: it cannot be magnified, and nothing is.
            move(-9000, -9000)
            move(100, 100)
            msg("zoom_in")
            harness.wait_for(lambda: zoom()[:2] == (2000, 2000), processes, "zoomed", detail=detail)
            harness.wait_for(lambda: "Cannot magnify HEADLESS-1" in log.read_text(), processes,
                             "the rotated output was tried", detail=detail)
            assert zoom()[2] == 0, zoom()

            # The pointer on the other output: that one is magnified regardless.
            move(int(second[2]) + 200, 0)
            harness.wait_for(lambda: zoom()[2] == 1, processes, "the second output magnified",
                             detail=detail)

            msg("zoom_reset")
            harness.wait_for(lambda: zoom() == (1000, 1000, 0), processes, "back to 1x",
                             detail=detail)
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("A rotated output shows 1x without disabling the magnifier elsewhere")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
