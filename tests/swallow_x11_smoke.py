# SPDX-License-Identifier: GPL-3.0-or-later
"""Window swallowing with an X11 window: the process of an XWayland window comes from its
_NET_WM_PID, so an X11 application started from a Wayland terminal takes the terminal's tile and
gives it back when it closes."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, x11_probe, wayland_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { swallow = { enabled = true, terminals = { "swallow-term" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-swallow-x11-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """app_id -> (tiled, x, y, width, height, visible)."""
        rows = {}
        for line in msg("get", "windows").splitlines():
            f = line.split("\t")
            rows[f[8]] = (f[3] == "1", int(f[4]), int(f[5]), int(f[6]), int(f[7]), f[11] == "1")
        return rows

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                     "startup", timeout=30)
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            env["DISPLAY"] = re.search(r"XWayland listening on DISPLAY=(\S+)", text)[1]

            terminal = subprocess.Popen(
                [wayland_probe, "--window-only"], stdout=subprocess.DEVNULL,
                env=dict(env, SHAODE_PROBE_APP_ID="swallow-term",
                         SHAODE_PROBE_SPAWN_PROGRAM=x11_probe))
            processes.append(terminal)
            wait_for(lambda: "swallow-term" in windows(), processes, "terminal mapped")
            other = subprocess.Popen([wayland_probe, "--window-only"], stdout=subprocess.DEVNULL,
                                     env=dict(env, SHAODE_PROBE_APP_ID="plain"))
            processes.append(other)
            wait_for(lambda: "plain" in windows(), processes, "neighbour mapped")
            msg("focus_last")  # back to the terminal, as if the user had it in front of them
            wait_for(lambda: msg("get", "windows").count("\t1\t0\t1\t") >= 1, processes, "focus")
            slot = windows()["swallow-term"]

            terminal.send_signal(10)  # SIGUSR1: start the X11 client from the terminal
            wait_for(lambda: "shaode-x11-probe" in windows(), processes, "X11 window mapped",
                     timeout=30)
            wait_for(lambda: not windows()["swallow-term"][5], processes, "terminal hidden")
            child = windows()["shaode-x11-probe"]
            assert child[0] and child[1:3] == slot[1:3] and child[5], (child, slot)
            swallow = msg("get", "swallow")
            assert "swallow-term\t1\t0\tshaode-x11-probe" in swallow, swallow
            assert "shaode-x11-probe\t0\t1\tswallow-term" in swallow, swallow

            # Closing the X11 window gives the terminal its tile back.
            subprocess.run([wayland_probe, "--close", "shaode-x11-probe"], env=env, check=True,
                           timeout=30, stdout=subprocess.DEVNULL)
            wait_for(lambda: "shaode-x11-probe" not in windows(), processes, "X11 window closed",
                     timeout=30)
            back = windows()["swallow-term"]
            assert back[5] and back[0] and back[1:3] == slot[1:3], (back, slot)

            terminal.terminate()
            terminal.wait(timeout=5)
            processes.remove(terminal)
            other.terminate()
            other.wait(timeout=5)
            processes.remove(other)
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("An X11 window swallows the terminal it was started from")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
