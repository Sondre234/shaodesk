# SPDX-License-Identifier: GPL-3.0-or-later
"""Night light: the compositor picks a colour temperature from the schedule (with the clock
fixed by SHAODE_NIGHT_LIGHT_TIME) and follows configuration and the toggle actions. The headless
backend has no gamma hardware and its screen capture is taken before the colour transform, so
what the screen shows needs a real output and is not covered here."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor = str(Path(sys.argv[1]).resolve())


def settings(extra):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 4 }},
    animations = {{ enabled = false }},
    night_light = {{ {extra} }},
}}"""


def start(directory, config, clock):
    """A compositor with the clock fixed at `clock`; returns (process, env, msg)."""
    log = Path(directory) / f"compositor-{clock.replace(':', '')}.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               SHAODE_NIGHT_LIGHT_TIME=clock, TZ="UTC")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)
    output = log.open("w")
    server = subprocess.Popen([compositor, "--headless", "--config", str(config)], env=env,
                              stdout=output, stderr=output)
    harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), [server], "startup")
    text = log.read_text()
    env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
    env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout
    return server, env, msg, log


def state(msg):
    """(kelvin, override, enabled)"""
    return tuple(int(n) for n in msg("get", "night_light").splitlines()[0].split("\t"))


with tempfile.TemporaryDirectory(prefix="shaode-night-test-") as directory:
    config = Path(directory) / "init.lua"
    manual = "enabled = true, night_temperature = 3400, sunrise = '07:00', sunset = '20:00', transition = 60"

    def run(clock, body, extra=manual):
        config.write_text(settings(extra))
        server, env, msg, log = start(directory, config, clock)
        processes = [server]
        try:
            body(server, env, msg, processes)
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)

    def noon(server, env, msg, processes):
        assert state(msg) == (6500, 0, 1), state(msg)
        # Forcing night, neutral, and back to the schedule.
        msg("night_light_on")
        assert state(msg) == (3400, 2, 1), state(msg)
        msg("night_light_toggle")
        assert state(msg) == (6500, 1, 1), state(msg)
        msg("night_light_toggle")
        assert state(msg)[:2] == (3400, 2), state(msg)
        msg("night_light_auto")
        assert state(msg) == (6500, 0, 1), state(msg)

    def midnight(server, env, msg, processes):
        assert state(msg) == (3400, 0, 1), state(msg)
        # Reloading with the schedule off ends it; the toggle still works.
        config.write_text(settings("enabled = false"))
        server.send_signal(signal.SIGHUP)
        harness.wait_for(lambda: state(msg) == (6500, 0, 0), processes, "schedule off")
        msg("night_light_toggle")
        assert state(msg) == (3400, 2, 0), state(msg)

    def dusk(server, env, msg, processes):
        # Sunset at 20:00 lasting an hour: at 20:00 the temperature is half way.
        k = state(msg)[0]
        assert abs(k - (6500 + 3400) / 2) <= 60, k

    def expect(kelvin_check, message):
        def body(server, env, msg, processes):
            assert kelvin_check(state(msg)[0]), (message, state(msg))
        return body

    run("12:00", noon)
    run("23:00", midnight)
    run("20:00", dusk)
    run("22:00", expect(lambda k: k == 3400, "night after the transition"))
    # A location gives the times: on the equator at the prime meridian (the clock runs in UTC)
    # the sun is up at noon and down at midnight, whatever the date.
    located = "enabled = true, latitude = 0, longitude = 0, transition = 10"
    run("12:00", expect(lambda k: k == 6500, "day at noon"), located)
    run("00:00", expect(lambda k: k == 3400, "night at midnight"), located)
    print("Night light follows the schedule and overrides")
