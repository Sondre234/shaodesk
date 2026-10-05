# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.scroll.follow: with "center" the view centers the column focus moves to, with "edge"
it moves only as far as needed, and with "never" focus alone does not move it."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "%s", width = 0.5 } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

# After three 640-wide columns the view shows the last two (B at 0, C at 640, A at -640).
# Focusing A then puts it at: 0 (edge: just brought into view), 320 (center), and -640 (never).
EXPECTED = {"edge": 0, "center": 320, "never": -640}

for follow, expected in EXPECTED.items():
    with tempfile.TemporaryDirectory(prefix="shaodesk-scroll-follow-") as directory:
        root = Path(directory)
        init = root / "init.lua"
        init.write_text(CONFIG % follow)
        log = root / "compositor.log"
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
        for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
            env.pop(name, None)

        def windows():
            result = subprocess.run([compositor, "msg", "get", "windows"], env=env,
                                    capture_output=True, text=True, timeout=5, check=True)
            rows = [line.split("\t") for line in result.stdout.splitlines()]
            return {r[9]: (r[1] == "1", int(r[4])) for r in rows}

        def wait_for(predicate, message):
            harness.wait_for(predicate, processes, message, detail=lambda: f"{follow}: {windows()}")

        with log.open("w") as output:
            server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                      env=env, stdout=output, stderr=output)
            processes = [server]
            try:
                wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
                text = log.read_text()
                env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
                env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
                for title in ("A", "B", "C"):
                    processes.append(subprocess.Popen(
                        [probe, "--window-only"],
                        env=dict(env, SHAODESK_PROBE_TITLE=title,
                                 SHAODESK_PROBE_APP_ID=f"app-{title.lower()}"),
                        stdout=subprocess.DEVNULL))
                    wait_for(lambda: title in windows() and windows()[title][0], f"{title} focused")
                # New windows move the view in every mode: C is the rightmost column, in view.
                wait_for(lambda: windows()["C"][1] + 640 == 1280 or follow == "center",
                         "view shows the new window")
                subprocess.run([probe, "--activate", "app-a"], env=env, check=True, timeout=5,
                               stdout=subprocess.DEVNULL)
                wait_for(lambda: windows()["A"][0], "A focused")
                # Let a running glide end, then check where A sits.
                wait_for(lambda: windows()["A"][1] == expected, f"A at {expected} with {follow}")
                server.terminate()
                assert server.wait(timeout=5) == 0, log.read_text()
            except Exception:
                print(log.read_text(), file=sys.stderr)
                raise
            finally:
                for process in reversed(processes):
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
print("scroll follow modes passed")
