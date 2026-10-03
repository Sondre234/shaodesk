# SPDX-License-Identifier: GPL-3.0-or-later
"""Random operations on window groups while windows open, close, move and change state, with a
seed: the compositor must survive, every group's members stay together on one workspace and
monitor with at most one showing, and turning groups off leaves no group behind."""
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20260929
steps = int(sys.argv[4]) if len(sys.argv) > 4 else 250

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 3 },
    animations = { enabled = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""

ACTIONS = ["group_toggle", "group_toggle", "ungroup", "group_next", "group_prev",
           "group_merge_left", "group_merge_right", "group_merge_up", "group_merge_down",
           "close", "toggle_floating", "toggle_tiling", "fullscreen", "toggle_sticky",
           "focus_left", "focus_right", "focus_next", "focus_prev", "swap_next", "promote",
           "layout_next", "layout_scroll", "layout_dwindle", "move_to_scratchpad",
           "scratchpad_show", "toggle_overview", "overview_cancel", "workspace_back"]

with tempfile.TemporaryDirectory(prefix="shaodesk-group-fuzz-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def run(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30)

    def windows():
        rows = [line.split("\t") for line in run("get", "windows").stdout.splitlines()]
        return [dict(workspace=r[0], title=r[9], monitor=r[10], visible=r[11] == "1",
                     scratchpad=r[12] == "1", sticky=r[13] == "1", group=int(r[14]))
                for r in rows]

    stacked = 0  # the largest group seen

    def check(step):
        global stacked
        groups = {}
        for w in windows():
            if w["group"]:
                groups.setdefault(w["group"], []).append(w)
        stacked = max([stacked] + [len(m) for m in groups.values()])
        for group, members in groups.items():
            if any(m["sticky"] or m["scratchpad"] for m in members):
                continue
            where = {(m["workspace"], m["monitor"]) for m in members}
            assert len(where) == 1, f"step {step}: group {group} is split: {members}"
            assert sum(m["visible"] for m in members) <= 1, \
                f"step {step}: group {group} shows several members: {members}"

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
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            rng = random.Random(seed)
            counter = 0

            def spawn():
                global counter
                counter += 1
                clients.append(subprocess.Popen(
                    [probe, "--window-only"],
                    env=dict(env, SHAODESK_PROBE_TITLE=f"win{counter}"),
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

            for _ in range(4):
                spawn()
            operations = [lambda a=a: run(a) for a in ACTIONS] + [
                lambda: run("workspace", str(rng.randint(1, 3))),
                lambda: run("move_to_workspace", str(rng.randint(1, 3))),
                lambda: run("output", rng.choice(["HEADLESS-1", "HEADLESS-2"]), "workspace",
                            str(rng.randint(1, 3))),
                spawn, spawn, spawn]
            for step in range(steps):
                rng.choice(operations)()
                assert server.poll() is None, f"the compositor died at step {step}"
                if step % 10 == 0:
                    check(step)
                if len(clients) > 10:
                    clients.pop(0).terminate()
            check(steps)
            assert stacked >= 2, "the fuzz never made a group of two windows"
            # Groups off: every group dissolves.
            init.write_text(CONFIG.replace("animations", "features = { groups = false },\n"
                                           "    animations"))
            run("reload")
            harness.wait_for(lambda: all(w["group"] == 0 for w in windows()), processes,
                             "the groups dissolved", detail=lambda: str(windows()))
            assert run("get", "windows").returncode == 0
            print(f"Group fuzz passed ({steps} steps, seed {seed}, largest group {stacked})")
        finally:
            for process in reversed(clients + processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
