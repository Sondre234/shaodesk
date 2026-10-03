# SPDX-License-Identifier: GPL-3.0-or-later
"""X11 windows that ask for attention (_NET_WM_STATE_DEMANDS_ATTENTION, the urgency flag of
WM_HINTS) while unfocused follow windows.activation like xdg-activation requests do."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import harness

compositor, x11_probe, wayland_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])


def settings(activation):
    return f"""return {{
    xwayland = true,
    layout = {{ tiling = false }},
    windows = {{ activation = "{activation}",
                 rules = {{ {{ title = "^Quiet X11", focus = false }} }} }},
}}"""


X11 = "shaode-x11-probe"

with tempfile.TemporaryDirectory(prefix="shaode-xurgent-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings("urgent"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=5, check=True).stdout

    def rows(request):
        return [line.split("\t") for line in msg("get", request).splitlines()]

    def urgent():
        return [r[8] for r in rows("urgent")]

    def focused():
        return [r[8] for r in rows("windows") if r[1] == "1"]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"urgent {urgent()} focused {focused()}")

    def tell(client, command):
        client.stdin.write(command + "\n")
        client.stdin.flush()

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup", timeout=10)
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["DISPLAY"] = re.search(r"XWayland listening on DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            x = subprocess.Popen([x11_probe, "commands"], env=env, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, text=True)
            processes.append(x)
            assert x.stdout.readline().strip() == "X11 window mapped and focused"
            assert x.stdout.readline().strip() == "waiting for commands"
            wait_for(lambda: focused() == [X11], "the X11 window is focused")
            w = subprocess.Popen([wayland_probe, "--commands"], env=env, stdin=subprocess.PIPE,
                                 stdout=subprocess.DEVNULL, text=True)
            processes.append(w)
            wait_for(lambda: focused() == ["shaode-probe"], "the Wayland window takes focus")
            assert urgent() == []

            # The state message, then the flag in WM_HINTS, each set and cleared by the client.
            tell(x, "demand")
            wait_for(lambda: urgent() == [X11], "demands attention marks the X11 window")
            assert focused() == ["shaode-probe"]
            tell(x, "undemand")
            wait_for(lambda: urgent() == [], "the client withdraws the demand")
            tell(x, "hint")
            wait_for(lambda: urgent() == [X11], "the urgency hint marks it")
            tell(x, "unhint")
            wait_for(lambda: urgent() == [], "the client clears the hint")
            tell(x, "hint")
            wait_for(lambda: urgent() == [X11], "the hint again")
            msg("focus_urgent")
            wait_for(lambda: focused() == [X11] and urgent() == [], "focus_urgent focuses it")

            # It is focused, so asking again changes nothing.
            tell(x, "demand")
            time.sleep(.3)
            assert urgent() == []
            tell(x, "undemand")
            subprocess.run([wayland_probe, "--activate", "shaode-probe"], env=env, check=True,
                           timeout=5, stdout=subprocess.DEVNULL)
            wait_for(lambda: focused() == ["shaode-probe"], "back to the Wayland window")

            # A window that asked before it mapped and opens without focus (a rule) is urgent from
            # the start.
            quiet = subprocess.Popen([x11_probe, "commands"], env=dict(
                env, SHAODE_PROBE_URGENT_ON_MAP="1", SHAODE_PROBE_TITLE="Quiet X11"),
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(quiet)
            assert "X11 window mapped" in quiet.stdout.readline()
            wait_for(lambda: [r[9] for r in rows("urgent")] == ["Quiet X11"],
                     "the window that asked before it mapped is urgent")
            assert focused() == ["shaode-probe"]
            msg("focus_urgent")
            wait_for(lambda: [r[9] for r in rows("windows") if r[1] == "1"] == ["Quiet X11"] and
                     urgent() == [], "and focus_urgent takes it")
            msg("close")
            assert quiet.wait(timeout=10) == 0
            processes.remove(quiet)
            wait_for(lambda: len(rows("windows")) == 2, "the quiet window closed")
            subprocess.run([wayland_probe, "--activate", "shaode-probe"], env=env, check=True,
                           timeout=5, stdout=subprocess.DEVNULL)
            wait_for(lambda: focused() == ["shaode-probe"], "back to the Wayland window")
            msg("focus_urgent")  # nothing urgent left
            assert focused() == ["shaode-probe"]

            # Under "focus" the request focuses it; under "ignore" nothing happens.
            config.write_text(settings("ignore"))
            msg("reload")
            wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
            tell(x, "demand")
            time.sleep(.4)
            assert urgent() == [] and focused() == ["shaode-probe"]
            tell(x, "undemand")
            config.write_text(settings("focus"))
            msg("reload")
            wait_for(lambda: log.read_text().count("Configuration reloaded") == 2, "reload")
            tell(x, "demand")
            wait_for(lambda: focused() == [X11] and urgent() == [], "focus policy focuses it")

            w.stdin.close()
            msg("close")
            x.stdin.close()
            assert x.wait(timeout=10) == 0
            processes.remove(x)
            w.terminate()
            w.wait(timeout=5)
            server.terminate()
            assert server.wait(timeout=10) == 0, log.read_text()
            print("X11 attention requests follow windows.activation")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
