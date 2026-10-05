# SPDX-License-Identifier: GPL-3.0-or-later
"""`get pid_at X Y`: the process of the window drawn at a layout point, nothing over bare
desktop, and an error without two numbers."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

with tempfile.TemporaryDirectory(prefix="shaodesk-pid-at-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(Path(example).read_text().replace("xwayland = true", "xwayland = false"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert "usage" in msg("get", "pid_at", "1", ok=False)
            assert msg("get", "pid_at", "-5000", "-5000") == ""

            client = subprocess.Popen([probe, "--external-control"], env=env,
                                      stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: msg("get", "windows").count("\n") == 1, processes, "window mapped")
            row = msg("get", "windows").split("\t")
            x, y, width, height = (int(value) for value in row[4:8])
            middle = (str(x + width // 2), str(y + height // 2))
            wait_for(lambda: msg("get", "pid_at", *middle) == f"{client.pid}\n", processes,
                     "the probe's pid at the middle of its window")

            msg("close")
            assert client.wait(timeout=30) == 0
            processes.remove(client)
            wait_for(lambda: msg("get", "pid_at", *middle) == "", processes,
                     "nothing at the closed window's place")

            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("get pid_at names the process of the window under a point")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
