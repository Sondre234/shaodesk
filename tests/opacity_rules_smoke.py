# SPDX-License-Identifier: GPL-3.0-or-later
"""Window opacity follows the rules through focus, title changes, and reloads, and the rules
are matched only when one of their inputs changes, not on every commit."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

import harness

compositor, client = (str(Path(p).resolve()) for p in sys.argv[1:3])


def settings(alpha):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = false }},
    layout = {{ tiling = true, workspaces = 1 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    windows = {{ border_width = 2, inactive_opacity = 0.5, rules = {{
        {{ app_id = "^solid$", opacity = 1, inactive_opacity = 1 }},
        {{ title = "^changed$", opacity = {alpha} }},
    }} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaode-opacity-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings(0.75))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout

    def opacities():
        """By title: focused, opacity."""
        rows = [line.split("\t") for line in msg("get", "opacities").splitlines()]
        return {r[1]: (r[2] == "1", float(r[3])) for r in rows}

    def rule_matches():
        return int(msg("get", "stats").split("\n")[0].split()[6])

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"opacities: {opacities()}")

    def open_window(app_id, title):
        processes.append(subprocess.Popen(
            [client, "--app-id", app_id, "--title", title], stdout=subprocess.DEVNULL, env=env))
        wait_for(lambda: title in opacities(), f"{title} opens")
        return processes[-1]

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            open_window("plain", "first")
            wait_for(lambda: opacities()["first"] == (True, 1.0), "focused window is opaque")
            second = open_window("plain", "second")
            wait_for(lambda: opacities()["second"] == (True, 1.0) and
                     opacities()["first"] == (False, 0.5), "focus moves the dimming")
            open_window("solid", "third")
            wait_for(lambda: opacities()["third"] == (True, 1.0) and
                     opacities()["second"] == (False, 0.5), "third focused")
            msg("focus_next")  # somebody else; the solid window keeps full opacity
            wait_for(lambda: not opacities()["third"][0], "focus left the solid window")
            assert opacities()["third"][1] == 1.0, opacities()

            # A title change re-matches the rules.
            second.send_signal(signal.SIGUSR1)
            wait_for(lambda: "changed" in opacities(), "title changes")
            focused = [t for t, (f, _) in opacities().items() if f][0]
            if focused == "changed":
                assert opacities()["changed"][1] == 0.75, opacities()
            else:
                assert opacities()["changed"][1] == 0.75 or opacities()["changed"][1] == 0.5

            # A window that commits on every frame does not have the rules matched each time.
            processes.append(subprocess.Popen(
                [client, "--animate", "--app-id", "plain", "--title", "busy"],
                stdout=subprocess.DEVNULL, env=env))
            wait_for(lambda: "busy" in opacities(), "busy window opens")
            wait_for(lambda: opacities()["busy"] == (True, 1.0), "busy window focused")
            commits = lambda: int(msg("get", "stats").split("\n")[0].split()[3])
            before, commits_before = rule_matches(), commits()
            harness.wait_for(lambda: commits() >= commits_before + 100, processes,
                             "busy window commits")
            assert rule_matches() - before < 5, "rules matched on every commit"

            # A reload with different rules takes effect for windows that did not change.
            config.write_text(settings(0.6))
            server.send_signal(signal.SIGHUP)
            wait_for(lambda: opacities()["changed"][1] in (0.6, 0.5) and
                     "Configuration reloaded" in log.read_text(), "reload applied")
            wait_for(lambda: opacities()["changed"][1] != 0.75, "old opacity replaced")
            if opacities()["changed"][0]:
                assert opacities()["changed"][1] == 0.6, opacities()
        finally:
            for process in processes[1:]:
                process.terminate()
            server.send_signal(signal.SIGTERM)
            server.wait(timeout=30)
    print("opacity_rules_smoke passed")
