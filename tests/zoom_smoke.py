# SPDX-License-Identifier: GPL-3.0-or-later
"""The magnifier: zoom_in and zoom_out ease the level, the output shows the scene enlarged around
the pointer (checked with grim where the capture sees what the output shows), zoom_reset returns
to 1x, and Super+scroll steps when configured. The pointer is moved with wlrctl."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""
wlrctl = shutil.which("wlrctl")

BACKGROUND = (0x00, 0x00, 0x00)
BODY = (0x41, 0x7b, 0xc4)


def settings(duration=400, extra=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 100 }},
    appearance = {{ background = '#000000' }},
    animations = {{ enabled = true, duration = 10 }},
    zoom = {{ step = 2, max = 4, duration = {duration}{extra} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaodesk-zoom-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings())
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout

    def zoom():
        """(level, target, magnified outputs), levels in thousandths"""
        return tuple(int(n) for n in msg("get", "zoom").splitlines()[0].split("\t"))

    def windows():
        return [line.split("\t") for line in msg("get", "windows").splitlines()]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"zoom: {zoom()}, windows: {windows()}")

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
            assert zoom() == (1000, 1000, 0), zoom()
            processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                              stdout=subprocess.DEVNULL))
            wait_for(lambda: len(windows()) == 1, "window")
            wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "opening animation over")
            x0, y0, w, h = (int(n) for n in windows()[0][4:8])
            assert x0 >= 50 and y0 >= 50, (x0, y0)

            # The pointer 50 px inside the window's left edge, half way down.
            px, py = x0 + 50, y0 + h // 2
            if wlrctl:
                move(-5000, -5000)
                move(px, py)
            probe_at = (x0 - 25, py)  # outside the window unless magnified 2x about the pointer
            if grim and wlrctl:
                assert harness.grab(grim, env).at(*probe_at) == BACKGROUND

            # Zooming in eases: in between, then arrived at 2x.
            msg("zoom_in")
            wait_for(lambda: 1000 < zoom()[0] < 2000 and zoom()[1] == 2000, "easing in")
            wait_for(lambda: zoom() == (2000, 2000, 1), "zoomed 2x")
            if grim and wlrctl:
                # The window's left edge is now 100 px further left than the pointer's 50.
                shot = harness.grab(grim, env)
                assert shot.at(*probe_at) == BODY, shot.at(*probe_at)

            # More steps stop at zoom.max; out again; reset.
            msg("zoom_in")
            msg("zoom_in")
            wait_for(lambda: zoom()[:2] == (4000, 4000), "limited to the maximum")
            msg("zoom_out")
            wait_for(lambda: zoom()[:2] == (2000, 2000), "one step out")
            msg("zoom_reset")
            wait_for(lambda: zoom() == (1000, 1000, 0), "back to 1x")
            if grim and wlrctl:
                assert harness.grab(grim, env).at(*probe_at) == BACKGROUND

            # A zero duration steps at once; the wheel needs the modifier configured here.
            config.write_text(settings(0, ", scroll_modifier = 'Super'"))
            msg("reload")
            msg("zoom_in")
            assert zoom()[:2] == (2000, 2000), zoom()
            msg("zoom_reset")
            wait_for(lambda: zoom() == (1000, 1000, 0), "reset")

            # Scrolling without the modifier leaves the zoom alone; with Super held it steps
            # (up zooms in, down zooms out).
            wtype = shutil.which("wtype")
            if wtype and wlrctl:
                def scroll(amount):
                    subprocess.run([wlrctl, "pointer", "scroll", str(amount), "0"], env=env,
                                   check=True, timeout=30)
                scroll(-15)
                time.sleep(0.3)
                assert zoom()[:2] == (1000, 1000), zoom()
                holder = subprocess.Popen([wtype, "-M", "logo", "-s", "2500", "-m", "logo"],
                                          env=env)
                processes.append(holder)
                time.sleep(0.5)
                scroll(-15)
                wait_for(lambda: zoom()[:2] == (2000, 2000), "wheel up zooms in")
                scroll(15)
                wait_for(lambda: zoom()[:2] == (1000, 1000), "wheel down zooms out")
                holder.wait(timeout=30)
                processes.remove(holder)
            else:
                print("wtype or wlrctl not found: scrolling not driven")

            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Zoom eases, magnifies around the pointer, and resets"
                  + ("" if grim and wlrctl else " (pixels not checked: grim or wlrctl missing)"))
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
