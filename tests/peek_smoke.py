# SPDX-License-Identifier: GPL-3.0-or-later
"""Peek fades every window toward the desktop while a key is held (or until toggled), and the
state follows the configuration; with grim the screen shows it, with wtype the held key works."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""

BODY = (0x41, 0x7b, 0xc4)  # what the probe paints below its title bar


def settings(opacity, duration, animations=True):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 4 }},
    appearance = {{ background = '#000000' }},
    peek = {{ opacity = {opacity}, duration = {duration} }},
    animations = {{ enabled = {str(animations).lower()}, duration = 10 }},
    bindings = {{ {{ mods = {{}}, key = 'F9', action = 'peek' }} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaodesk-peek-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings(0.25, 400))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30, check=True)
        return result.stdout

    def peek():
        """(progress in thousandths, peeking, held by a key)"""
        return tuple(int(n) for n in msg("get", "peek").splitlines()[0].split("\t"))

    def windows():
        return [line.split("\t") for line in msg("get", "windows").splitlines()]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"peek: {peek()}, windows: {windows()}")

    def body_pixel():
        row = windows()[0]
        x, y, w, h = (int(n) for n in row[4:8])
        return harness.grab(grim, env).at(x + w // 2, y + h // 2)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert peek() == (0, 0, 0), peek()
            processes.append(subprocess.Popen([probe, "--external-control"], env=env,
                                              stdout=subprocess.DEVNULL))
            wait_for(lambda: len(windows()) == 1, "window")
            wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "opening animation over")
            if grim:
                assert body_pixel() == BODY, body_pixel()

            # Toggling from the control socket fades in over the duration, without jumping.
            msg("peek_toggle")
            wait_for(lambda: 0 < peek()[0] < 1000 and peek()[1] == 1, "fade in progress")
            wait_for(lambda: peek() == (1000, 1, 0), "peeking")
            if grim:
                # Black background: the body colour at 25%.
                got = body_pixel()
                assert all(abs(g - round(w * 0.25)) <= 3 for g, w in zip(got, BODY)), got
            msg("peek_toggle")
            wait_for(lambda: peek() == (0, 0, 0), "fade out")
            if grim:
                assert body_pixel() == BODY, body_pixel()

            # A held key: down peeks, up ends it.
            wtype = shutil.which("wtype")
            if wtype:
                holder = subprocess.Popen([wtype, "-P", "F9", "-s", "1500", "-p", "F9"], env=env)
                processes.append(holder)
                wait_for(lambda: peek()[1:] == (1, 1), "held key peeks")
                holder.wait(timeout=30)
                processes.remove(holder)
                wait_for(lambda: peek() == (0, 0, 0), "released key ends it")
            else:
                print("wtype not found: held key skipped")

            # Immediate with animations off or a zero duration.
            for immediate in (settings(0.25, 400, animations=False), settings(0.25, 0)):
                config.write_text(immediate)
                msg("reload")
                msg("peek_toggle")
                assert peek() == (1000, 1, 0), peek()
                msg("peek_toggle")
                assert peek() == (0, 0, 0), peek()

            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Peek fades windows out and back, held or toggled"
                  + ("" if grim else " (pixels not checked: grim missing)"))
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
