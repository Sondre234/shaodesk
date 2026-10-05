# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile maximized by hand floats over the tiling, but a new window opening on its workspace
brings it back in, as it would a fullscreen one, instead of covering it. Turning tiling off and
on again also brings back windows that float only because they were maximized."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

with tempfile.TemporaryDirectory(prefix="shaodesk-maximized-tile-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(Path(example).read_text().replace("xwayland = true", "xwayland = false")
                      .replace("tiling = false", "tiling = true"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """(tiled, x, y, width, height) per window, oldest first."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return [(r[3] == "1", *map(int, r[4:8])) for r in rows]

    def disjoint(rects):
        return all(a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or
                   a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1]
                   for i, a in enumerate(rects) for b in rects[i + 1:])

    def all_tiled(count):
        current = windows()
        return (len(current) == count and all(w[0] for w in current) and
                disjoint([w[1:] for w in current]))

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]

        def wait_for(predicate, message):
            harness.wait_for(predicate, processes, message,
                             detail=lambda: f"windows: {windows()}")

        def launch():
            processes.append(subprocess.Popen([probe, "--window-only"], env=env,
                                              stdout=subprocess.DEVNULL,
                                              stderr=subprocess.DEVNULL))

        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert msg("get", "tiling") == "on\n"

            launch()
            wait_for(lambda: all_tiled(1), "first window tiled")
            msg("maximize")
            wait_for(lambda: not windows()[0][0], "maximizing floats the tile")

            # The new window tiles beside the maximized one instead of opening over it.
            launch()
            wait_for(lambda: all_tiled(2), "maximized window back in the tiling")

            # Turning tiling off and on brings a maximized window back too.
            msg("maximize")
            wait_for(lambda: sum(w[0] for w in windows()) == 1, "second window maximized")
            msg("toggle_tiling")
            wait_for(lambda: msg("get", "tiling") == "off\n", "tiling off")
            msg("toggle_tiling")
            wait_for(lambda: all_tiled(2), "both tiled after tiling is on again")
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("A maximized tile comes back into the tiling")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                process.terminate()
            for process in reversed(processes):
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
