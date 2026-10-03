# SPDX-License-Identifier: GPL-3.0-or-later
"""Open, reflow, and close windows with animations on; each animation ends and leaves nothing."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def settings(enabled, duration):
    # Long enough that the test sees each animation running.
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true }},
    windows = {{ border_width = 2, inactive_opacity = 0.8 }},
    animations = {{ enabled = {str(enabled).lower()}, duration = {duration} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaode-animation-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings(True, 1000))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30, check=True)
        return result.stdout

    def state():
        """(running animations, window trees stacked in the scene)"""
        running, stacked, _fading = msg("get", "animations").split("\t")
        return int(running), int(stacked)

    def fading():
        """Focus fades (opacity and border color) in flight."""
        return int(msg("get", "animations").split("\t")[2])

    def windows():
        return [line.split("\t") for line in msg("get", "windows").splitlines()]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"animations: {state()}, windows: {windows()}")

    def launch():
        window = subprocess.Popen([probe, "--external-control"], env=env,
                                  stdout=subprocess.DEVNULL)
        processes.append(window)
        return window

    def close(window):
        window.terminate()
        window.wait(timeout=30)
        processes.remove(window)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert state() == (0, 0), state()

            # Opening: the window is placed at once, while its animation runs.
            first = launch()
            wait_for(lambda: len(windows()) == 1 and state()[0] == 1, "first window opening")
            wait_for(lambda: state() == (0, 1), "opening animation finished")

            # A second tile opens and the first glides aside; both end.
            second = launch()
            wait_for(lambda: len(windows()) == 2 and state()[0] == 2, "open and glide running")
            # The first window lost focus, so its opacity and border fade.
            wait_for(lambda: fading() >= 1, "focus fade running")
            wait_for(lambda: state() == (0, 2), "open and glide finished")
            wait_for(lambda: fading() == 0, "focus fade finished")

            # Closing leaves a copy behind for the animation, which then goes, and the remaining
            # tile glides back.
            close(second)
            wait_for(lambda: len(windows()) == 1 and state() == (2, 2), "closing copy and glide")
            wait_for(lambda: state() == (0, 1), "closing copy removed")
            close(first)
            wait_for(lambda: not windows() and state()[1] <= 1, "last window closed")
            wait_for(lambda: state() == (0, 0), "nothing left behind")

            # Switching workspace slides a copy of the old windows away and brings the new ones
            # in; input never waits for either.
            left = launch()
            wait_for(lambda: len(windows()) == 1 and state() == (0, 1), "window on workspace 1")
            msg("workspace", "2")
            assert msg("get", "workspace") == "2\n"
            wait_for(lambda: state() == (1, 2), "copy sliding away")
            wait_for(lambda: state() == (0, 1), "copy gone")
            right = launch()
            wait_for(lambda: len(windows()) == 2 and state() == (0, 2), "window on workspace 2")
            msg("workspace", "1")
            wait_for(lambda: state() == (2, 3), "one window out, one in")
            assert msg("get", "workspace") == "1\n"
            wait_for(lambda: state() == (0, 2), "slide finished")
            close(left)
            close(right)
            wait_for(lambda: not windows() and state()[0] == 0, "workspace windows closed")
            msg("workspace", "1")
            wait_for(lambda: state() == (0, 0), "nothing left behind")

            # Fullscreen toggles glide the window, and leaving lands it at its old place.
            floating = launch()
            wait_for(lambda: len(windows()) == 1 and state() == (0, 1), "window for fullscreen")
            msg("fullscreen")
            wait_for(lambda: state()[0] >= 1, "fullscreen glide running")
            wait_for(lambda: state()[0] == 0, "fullscreen glide finished")
            msg("fullscreen")
            wait_for(lambda: state()[0] == 0, "leaving fullscreen settles")
            close(floating)
            wait_for(lambda: not windows() and state() == (0, 0), "fullscreen window closed")

            # Turning animations off ends those running.
            windows_open = [launch(), launch()]
            wait_for(lambda: len(windows()) == 2, "two windows")
            config.write_text(settings(False, 1000))
            server.send_signal(signal.SIGHUP)
            wait_for(lambda: state() == (0, 2), "reload ends animations")
            for window in windows_open:
                close(window)
            wait_for(lambda: state() == (0, 0), "closed without animations")

            # Quitting mid-animation frees the copies.
            config.write_text(settings(True, 1000))
            server.send_signal(signal.SIGHUP)
            wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
            window = launch()
            wait_for(lambda: len(windows()) == 1, "window before quitting")
            close(window)
            wait_for(lambda: state()[0] >= 1, "closing animation before quitting")
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Opening, glide, closing snapshots, disabling, and quitting mid-animation passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
