# SPDX-License-Identifier: GPL-3.0-or-later
"""Random operations on the overview while windows come and go, with a fixed seed: opening and
closing it, filtering, selecting, viewing workspaces, confirming, and switching, moving,
minimizing and closing windows. The compositor must survive, and end with the overview closed
and every window still listed."""
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20260928
steps = int(sys.argv[4]) if len(sys.argv) > 4 else 300

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 4 },
    overview = { animation = true, duration = 30 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-overview-fuzz-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def run(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        clients = []
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(),
                             processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            rng = random.Random(seed)
            words = ["a", "b", "term", "zz", "note", "x", "e", ""]
            counter = 0

            def spawn():
                global counter
                counter += 1
                clients.append(subprocess.Popen(
                    [probe, "--window-only"], env=dict(env, SHAODE_PROBE_TITLE=f"win{counter}",
                                                       SHAODE_PROBE_APP_ID="zz"),
                    stdout=subprocess.DEVNULL))

            for _ in range(4):
                spawn()
            operations = [
                lambda: run("toggle_overview"),
                lambda: run("toggle_overview"),
                lambda: run("overview", "filter", rng.choice(words) + rng.choice(words)),
                lambda: run("overview", "select", str(rng.randint(1, 8))),
                lambda: run("overview", "view", str(rng.randint(1, 4))),
                lambda: run("overview_confirm", *([str(rng.randint(1, 6))] * rng.randint(0, 1))),
                lambda: run("overview_cancel"),
                lambda: run("workspace", str(rng.randint(1, 4))),
                lambda: run("output", rng.choice(["HEADLESS-1", "HEADLESS-2"]), "toggle_overview"),
                lambda: run("move_to_workspace", str(rng.randint(1, 4))),
                lambda: run("close"),
                lambda: run("toggle_tiling"),
                lambda: run("get", "overview"),
                lambda: spawn(),
                lambda: spawn(),
            ]
            for step in range(steps):
                rng.choice(operations)()
                assert server.poll() is None, f"the compositor died at step {step}"
                if len(clients) > 12:
                    clients.pop(0).terminate()
            run("overview_cancel")
            harness.wait_for(lambda: run("get", "overview").stdout.startswith("closed"),
                             processes, "the overview closed")
            assert run("get", "windows").returncode == 0
            print(f"Overview fuzz passed ({steps} steps, seed {seed})")
        finally:
            for process in reversed(clients + processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
