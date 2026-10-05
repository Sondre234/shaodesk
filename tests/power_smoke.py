# SPDX-License-Identifier: GPL-3.0-or-later
"""The power actions against a fake logind (tests/fake_login1.c) on a private bus, a dbus-daemon
this test starts and kills: the compositor reaches it through SHAODESK_LOGIN1_BUS and never the
machine's own logind, and a headless compositor without that variable has no logind at all."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import harness

compositor, fake_login1, probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the power actions were not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-power-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    (root / "bus.conf").write_text(
        f'<busconfig><type>session</type><listen>unix:dir={directory}</listen>'
        '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/>'
        '<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
    calls, answers = root / "login1.log", root / "answers"
    calls.touch()
    answers.write_text("CanReboot challenge\nCanHibernate na\n")
    compositor_log = root / "compositor.log"
    processes = []
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET", "SHAODESK_LOGIN1_BUS"):
        env.pop(name, None)

    def wait_for(predicate, message, timeout=15):
        harness.wait_for(predicate, processes, message, timeout=timeout,
                         detail=lambda: f"logind: {calls.read_text().splitlines()}")

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def power():
        """What `get power` says of each action, and of the one pending."""
        return dict(line.split("\t") for line in msg("get", "power").splitlines())

    def answers_are(**expected):
        return lambda: all(power()[name] == value for name, value in expected.items())

    def logged():
        """What the fake logind recorded, after its "ready"."""
        return calls.read_text().splitlines()[1:]

    def start(output, extra_env):
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env={**env, **extra_env}, stdout=output, stderr=output)
        processes.append(server)
        wait_for(lambda: "Running Wayland compositor" in compositor_log.read_text(), "startup")
        text = compositor_log.read_text()
        env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
        return server

    def stop(server):
        processes.remove(server)
        server.terminate()
        assert server.wait(timeout=30) == 0, compositor_log.read_text()

    try:
        bus = subprocess.Popen(["dbus-daemon", f"--config-file={root / 'bus.conf'}", "--nofork",
                                "--print-address=1"], stdout=subprocess.PIPE, text=True)
        processes.append(bus)
        address = bus.stdout.readline().strip()
        assert address.startswith("unix:"), address
        login1 = subprocess.Popen([fake_login1, address, str(calls), str(answers)])
        processes.append(login1)
        wait_for(lambda: "ready" in calls.read_text(), "the fake logind")

        # Headless, and not told of a bus, the compositor leaves logind alone.
        with compositor_log.open("w") as output:
            server = start(output, {})
            assert power() == {"poweroff": "unavailable", "reboot": "unavailable",
                               "suspend": "unavailable", "hibernate": "unavailable",
                               "pending": "-"}, power()
            assert "SHAODESK_LOGIN1_BUS" in compositor_log.read_text()
            assert "logind is out of reach" in msg("poweroff", ok=False)
            assert "takes no argument" in msg("reboot", "now", ok=False)
            stop(server)
        assert logged() == [], logged()

        with compositor_log.open("w") as output:
            server = start(output, {"SHAODESK_LOGIN1_BUS": address})
            # What logind allows arrives soon after startup.
            wait_for(answers_are(poweroff="yes", reboot="challenge", suspend="yes",
                                 hibernate="na", pending="-"), "logind's answers")

            # Power off and reboot go to logind, which may ask for a password (interactive).
            msg("poweroff")
            wait_for(lambda: logged() == ["PowerOff true"], "power off")
            wait_for(answers_are(pending="-"), "logind's reply to power off")
            msg("reboot")
            wait_for(lambda: logged()[1:] == ["Reboot true"], "reboot")
            wait_for(answers_are(pending="-"), "logind's reply to reboot")

            # What logind refuses is not asked for; a reload asks what it allows afresh.
            answers.write_text("CanPowerOff na\nCanReboot no\n")
            msg("reload")
            wait_for(answers_are(poweroff="na", reboot="no"), "the new answers")
            assert "logind does not allow power off here (CanPowerOff: na)" in msg(
                "poweroff", ok=False)
            assert "logind does not allow reboot here (CanReboot: no)" in msg("reboot", ok=False)

            # A call logind turns down is reported, and leaves nothing pending.
            answers.write_text("fail Reboot\n")
            msg("reload")
            wait_for(answers_are(poweroff="yes", reboot="yes"), "the answers back to yes")
            msg("reboot")
            wait_for(lambda: "Reboot failed: Access denied by the fake logind"
                     in compositor_log.read_text(), "the refusal reported")
            assert power()["pending"] == "-", power()
            stop(server)
        assert logged() == ["PowerOff true", "Reboot true", "Reboot true"], logged()
        print("The compositor asks only the logind it is given, which powers off and reboots "
              "when it allows them")
    except Exception:
        print(compositor_log.read_text() if compositor_log.exists() else "", file=sys.stderr)
        raise
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
