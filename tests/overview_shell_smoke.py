# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell draws the overview's text over the compositor's thumbnails: it shows an overlay
while the overview is open on its output, hides it after, and the overlay puts each window's
title and the search box on screen (found as light pixels on a screenshot)."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4, workspace_names = { "web", "code" } },
    overview = { animation = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-overview-shell-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    compositor_log, shell_log = root / "compositor.log", root / "shell.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=directory, XDG_DATA_DIRS=directory,
               DBUS_SESSION_BUS_ADDRESS="disabled:")  # the shell must not use the real session bus
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    processes = []

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30, check=True).stdout

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, timeout=30,
                         detail=lambda: shell_log.read_text()[-600:])

    with compositor_log.open("w") as out, shell_log.open("w") as shell_out:
        try:
            server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                      env=env, stdout=out, stderr=out)
            processes.append(server)
            wait_for(lambda: "Running Wayland compositor" in compositor_log.read_text(),
                     "compositor startup")
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)",
                                               compositor_log.read_text())[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)",
                                             compositor_log.read_text())[1]
            processes.append(subprocess.Popen([shell, "--config", str(config)], env=env,
                                              stdout=shell_out, stderr=shell_out))
            wait_for(lambda: "shaodesk surface rendered: shaodesk taskbar" in shell_log.read_text(),
                     "the panel rendered")
            for title in ("Terminal", "Notes"):
                processes.append(subprocess.Popen(
                    [probe, "--window-only"], env=dict(env, SHAODESK_PROBE_TITLE=title,
                                                       SHAODESK_PROBE_APP_ID="zz"),
                    stdout=subprocess.DEVNULL))
                wait_for(lambda: title in msg("get", "windows"), f"{title} mapped")

            # The shell subscribes asynchronously: open until it has heard.
            def opened():
                msg("toggle_overview")
                try:
                    harness.wait_for(lambda: "shaodesk overview shown" in shell_log.read_text(),
                                     processes, "overlay", timeout=1)
                    return True
                except AssertionError:
                    msg("overview_cancel")
                    return False
            for _ in range(10):
                if opened():
                    break
            assert "shaodesk overview shown on HEADLESS-1" in shell_log.read_text()

            grim = shutil.which("grim")
            if grim:
                def light_pixels(x, y, w, h, shot, level=150):
                    return sum(1 for j in range(y, y + h) for i in range(x, x + w)
                               if min(shot.at(i, j)) > level)

                def titled():
                    shot = harness.grab(grim, env)
                    lines = msg("get", "overview").splitlines()[2:]
                    windows = [l.split(" ", 5) for l in lines if l.startswith("overview-window")]
                    assert len(windows) == 2, lines
                    for _, x, y, w, h, _ in windows:
                        x, y, w, h = int(x), int(y), int(w), int(h)
                        if not light_pixels(x + 24, y + h - 24, w - 30, 24, shot) > 20:
                            return False
                    # The search box: its placeholder text is light on the panel colour.
                    return light_pixels(440, 8, 400, 32, shot, 90) > 20
                wait_for(titled, "titles and the search box drawn")
                print("Text checked")
            msg("overview_cancel")
            wait_for(lambda: "shaodesk overview hidden" in shell_log.read_text(), "overlay hidden")
            print("Overview shell passed")
        finally:
            for process in reversed(processes):
                process.terminate()
                process.wait(timeout=30)
