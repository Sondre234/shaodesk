# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell marks a window that asks for attention on the taskbar, from the compositor's
subscription and the foreign-toplevel list, and unmarks it once it has focus: seen as pixels in
the urgent color in the panel's screenshot band."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""
if not grim:
    print("grim is missing: the taskbar's urgent marker was not checked")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { urgent_color = "#ff9e64" },
    shell = { panel_height = 52 },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-urgent-shell-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    compositor_log, shell_log = root / "compositor.log", root / "shell.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=directory, XDG_DATA_DIRS=directory)
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    processes = []

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=5, check=True).stdout

    def orange():
        """How many pixels in the panel's band are the urgent color."""
        shot = harness.grab(grim, env)
        count = 0
        for y in range(shot.height - 52, shot.height):
            for x in range(0, shot.width, 1):
                r, g, b = shot.at(x, y)
                count += r > 200 and 120 < g < 190 and 60 < b < 140
        return count

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, timeout=10,
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
            clients = {}
            for name in ("Alpha", "Beta"):
                client = subprocess.Popen(
                    [probe, "--commands"], env=dict(env, SHAODESK_PROBE_TITLE=name,
                                                    SHAODESK_PROBE_APP_ID=name.lower()),
                    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
                processes.append(client)
                clients[name] = client
                wait_for(lambda: name in msg("get", "windows"), f"{name} mapped")
            wait_for(lambda: orange() == 0, "a quiet panel has no urgent color")

            clients["Alpha"].stdin.write("activate\n")
            clients["Alpha"].stdin.flush()
            wait_for(lambda: "alpha" in msg("get", "urgent"), "the compositor marks Alpha")
            wait_for(lambda: orange() >= 20, "the taskbar shows the urgent window")
            marked = orange()

            # The overview frames the thumbnail of the window asking for attention.
            def rims():
                """{title: rim pixel} of each thumbnail's top edge, or None while closed."""
                lines = msg("get", "overview").splitlines()
                if len(lines) < 3:
                    return None
                shot = harness.grab(grim, env)
                found = {}
                for line in lines[2:]:
                    if not line.startswith("overview-window"):
                        continue
                    _, x, y, w, h, tail = line.split(" ", 5)
                    found[tail.split("\t")[1]] = shot.at(int(x) + int(w) // 2, int(y) + 1)
                return found

            def is_orange(pixel):
                return pixel[0] > 200 and 120 < pixel[1] < 190 and 60 < pixel[2] < 140

            def overview_marks():
                msg("toggle_overview")
                harness.wait_for(lambda: "shaodesk overview shown" in shell_log.read_text(),
                                 processes, "the overview's text", timeout=5)
                harness.wait_for(lambda: rims() and len(rims()) == 2, processes, "two thumbnails")
                return None

            overview_marks()
            wait_for(lambda: (r := rims()) and is_orange(r["Alpha"]) and not is_orange(r["Beta"]),
                     "the overview frames only Alpha's thumbnail")
            msg("overview_cancel")

            msg("focus_urgent")
            wait_for(lambda: msg("get", "urgent") == "", "focus clears the mark")
            wait_for(lambda: orange() == 0, "the taskbar's marker goes with it")

            for client in clients.values():
                client.stdin.close()
            for process in processes[2:]:
                process.terminate()
                process.wait(timeout=5)
            processes[1].terminate()
            processes[1].wait(timeout=5)
            server.terminate()
            assert server.wait(timeout=5) == 0, compositor_log.read_text()
            print(f"The taskbar marks a window asking for attention ({marked} pixels of the urgent "
                  "color) and unmarks it on focus")
        except Exception:
            print(compositor_log.read_text(), file=sys.stderr)
            print(shell_log.read_text()[-2000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
