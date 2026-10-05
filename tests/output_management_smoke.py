# SPDX-License-Identifier: GPL-3.0-or-later
"""wlr-output-management: a client (as wlr-randr is) lists the outputs, tests and applies changes,
and a reload of the Lua configuration takes them back."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with tempfile.TemporaryDirectory(prefix="shaodesk-randr-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text("return { xwayland = false }")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="3")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def outputs():
        result = subprocess.run([compositor, "msg", "get", "outputs"], env=env,
                                capture_output=True, text=True, timeout=30, check=True)
        rows = [line.split("\t") for line in result.stdout.splitlines()]
        return {row[0]: (row[1] == "1", int(row[2]), int(row[3]), int(row[4]), int(row[5]),
                         float(row[6]), int(row[7])) for row in rows}

    def randr(*args):
        result = subprocess.run([probe, *args], env=env, capture_output=True, text=True,
                                timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split("\n")[:-1]

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), [server], "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            heads = {line.split()[0]: line.split() for line in randr("list")}
            assert sorted(heads) == ["HEADLESS-1", "HEADLESS-2", "HEADLESS-3"], heads
            assert all(head[1] == "1" for head in heads.values()), heads
            before = outputs()

            # A test changes nothing.
            assert randr("test", "HEADLESS-2", "scale=2") == ["succeeded"]
            assert outputs() == before

            assert randr("apply", "HEADLESS-2", "scale=2", "transform=1") == ["succeeded"]
            state = outputs()
            assert state["HEADLESS-2"][5:7] == (2.0, 1), state
            # The change reaches clients listing the heads afterwards.
            heads = {line.split()[0]: line.split() for line in randr("list")}
            assert heads["HEADLESS-2"][4:6] == ["2", "1"], heads

            assert randr("apply", "HEADLESS-1", "x=3000", "y=40") == ["succeeded"]
            heads = {line.split()[0]: line.split() for line in randr("list")}
            assert sorted(heads) == ["HEADLESS-1", "HEADLESS-2", "HEADLESS-3"], heads
            state = outputs()
            assert state["HEADLESS-1"][2] == 40 and state["HEADLESS-1"][1] > state["HEADLESS-2"][1], state

            assert randr("apply", "HEADLESS-3", "enabled=0") == ["succeeded"]
            assert outputs()["HEADLESS-3"][0] is False
            assert randr("apply", "HEADLESS-3", "enabled=1") == ["succeeded"]
            assert outputs()["HEADLESS-3"][0] is True

            # Turning every output off is refused.
            assert randr("apply", "HEADLESS-1", "enabled=0") == ["succeeded"]
            assert randr("apply", "HEADLESS-2", "enabled=0") == ["succeeded"]
            assert randr("test", "HEADLESS-3", "enabled=0") == ["failed"]
            assert sum(enabled for enabled, *_ in outputs().values()) == 1

            # A reload restores what the configuration says.
            server.send_signal(signal.SIGHUP)
            wait_for(lambda: "Configuration reloaded" in log.read_text(), [server], "reload")
            state = outputs()
            assert all(row[0] and row[5:7] == (1.0, 0) for row in state.values()), state

            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Output management test, apply, and reload passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            if server.poll() is None:
                server.kill()
