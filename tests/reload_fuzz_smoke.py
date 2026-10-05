# SPDX-License-Identifier: GPL-3.0-or-later
"""Configuration reloads under load: with windows open across workspaces and monitors, the
configuration is rewritten at random (workspace count, monitors enabled, scaled, rotated or
resized, layouts, groups, animations, overview, zoom) and reloaded between random actions. The
compositor must survive and every window must stay listed."""
import os
from pathlib import Path
import random
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20260929
steps = int(sys.argv[4]) if len(sys.argv) > 4 else 120


def config(rng):
    def monitor(name):
        fields = []
        if rng.random() < .2:
            fields.append("enabled = false")
        fields.append(f'mode = "{rng.choice(["1280x720", "800x600", "1024x768"])}"')
        if rng.random() < .3:
            fields.append(f"scale = {rng.choice([1, 1.5, 2])}")
        if rng.random() < .3:
            fields.append(f"transform = {rng.randint(0, 7)}")
        if rng.random() < .3:
            fields.append(f"position = {{ x = {rng.randint(0, 3000)}, y = {rng.randint(0, 500)} }}")
        if rng.random() < .3:
            fields.append(f"tiling = {rng.choice(['true', 'false'])}")
        return f'["{name}"] = {{ {", ".join(fields)} }}'
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = {rng.choice(['true', 'false'])}, workspaces = {rng.randint(1, 6)},
               gap = {rng.randint(0, 30)}, tile_layout = "{rng.choice(['dwindle', 'master', 'spiral', 'monocle', 'scroll'])}" }},
    animations = {{ enabled = {rng.choice(['true', 'false'])}, duration = {rng.randint(10, 200)} }},
    features = {{ groups = {rng.choice(['true', 'false'])}, scratchpad = {rng.choice(['true', 'false'])},
                 sticky = {rng.choice(['true', 'false'])}, group_join_new = {rng.choice(['true', 'false'])} }},
    overview = {{ enabled = {rng.choice(['true', 'false'])}, animation = {rng.choice(['true', 'false'])} }},
    zoom = {{ step = {rng.choice([1.25, 2])}, max = {rng.choice([2, 4, 8])}, duration = {rng.randint(0, 300)} }},
    outputs = {{ order = {{ "HEADLESS-1", "HEADLESS-2" }},
                monitors = {{ {monitor("HEADLESS-1")}, {monitor("HEADLESS-2")} }} }},
}}"""


ACTIONS = ["group_toggle", "ungroup", "group_next", "toggle_tiling", "toggle_floating", "close",
           "fullscreen", "maximize", "toggle_overview", "overview_cancel", "zoom_in", "zoom_out",
           "zoom_reset", "night_light_toggle", "move_to_scratchpad", "scratchpad_show",
           "layout_next", "workspace_back", "peek_toggle", "toggle_sticky", "focus_next",
           "cycle", "switcher", "switcher_cancel"]

with tempfile.TemporaryDirectory(prefix="shaodesk-reload-fuzz-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    rng = random.Random(seed)
    init.write_text("return { xwayland = false, layout = { workspaces = 4 } }")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
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
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            counter = 0

            def spawn():
                global counter
                counter += 1
                clients.append(subprocess.Popen(
                    [probe, "--window-only"], env=dict(env, SHAODESK_PROBE_TITLE=f"win{counter}"),
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

            reloads = 0

            def reload():
                global reloads
                init.write_text(config(rng))
                result = run("reload")
                assert result.returncode == 0, (result.stdout, result.stderr)
                reloads += 1

            for _ in range(5):
                spawn()
            operations = [lambda a=a: run(a) for a in ACTIONS] + [
                lambda: run("workspace", str(rng.randint(1, 6))),
                lambda: run("move_to_workspace", str(rng.randint(1, 6))),
                spawn, spawn, reload, reload, reload]
            for step in range(steps):
                rng.choice(operations)()
                assert server.poll() is None, f"the compositor died at step {step}"
                if len(clients) > 10:
                    clients.pop(0).terminate()
            assert reloads >= 5, reloads
            # Every random configuration was valid, and so took effect.
            applied = log.read_text().count("Configuration reloaded")
            assert applied == reloads, (applied, reloads, log.read_text()[-2000:])
            # Back to a plain configuration: the windows are all still there.
            init.write_text("return { xwayland = false, layout = { workspaces = 4 } }")
            assert run("reload").returncode == 0
            harness.wait_for(lambda: len(run("get", "windows").stdout.splitlines()) ==
                             sum(client.poll() is None for client in clients), processes,
                             "every live client's window listed")
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print(f"Reload fuzz passed ({steps} steps, seed {seed})")
        except Exception:
            print(log.read_text()[-3000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(clients + processes):
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=30)
