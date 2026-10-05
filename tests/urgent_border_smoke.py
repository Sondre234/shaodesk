# SPDX-License-Identifier: GPL-3.0-or-later
"""The border of an urgent window pulses for a few seconds, then holds windows.urgent_color, with
a border_width or (inside the window's edge) without one, and gives way to the focus color."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""
if not grim:
    print("grim is missing: urgent borders not checked")
    sys.exit(0)


def settings(border, animations="true"):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = false }},
    animations = {{ enabled = {animations} }},
    windows = {{ border_width = {border}, urgent_color = "#ff9e64", border_color = "#7da8ff",
                 border_inactive_color = "#404a5c" }},
}}"""


URGENT, FOCUSED, INACTIVE = (255, 158, 100), (125, 168, 255), (64, 74, 92)


def near(pixel, want, tolerance=3):
    return all(abs(a - b) <= tolerance for a, b in zip(pixel, want))


with tempfile.TemporaryDirectory(prefix="shaodesk-urgent-border-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings(3))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=5, check=True).stdout

    def windows():
        return {r[8]: r for r in (l.split("\t") for l in msg("get", "windows").splitlines())}

    def edge(app_id, inside):
        """The pixel on the middle of the window's left edge: outside it, or just inside."""
        row = windows()[app_id]
        x, y, h = int(row[4]), int(row[5]), int(row[7])
        return harness.grab(grim, env).at(x + (1 if inside else -2), y + h // 2)

    def start(app_id):
        client = subprocess.Popen([probe, "--commands"], env=dict(env, SHAODESK_PROBE_APP_ID=app_id),
                                  stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
        processes.append(client)
        harness.wait_for(lambda: windows().get(app_id, [0, "0"])[1] == "1", processes,
                         f"{app_id} focused")
        return client

    def ask(client):
        client.stdin.write("activate\n")
        client.stdin.flush()

    def urgent_count():
        return len(msg("get", "urgent").splitlines())

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            a = start("urgent-a")
            b = start("urgent-b")
            # Move the windows apart so neither's edge is under the other: b right, a left.
            msg("move_right")
            subprocess.run([probe, "--activate", "urgent-a"], env=env, check=True, timeout=5,
                           stdout=subprocess.DEVNULL)
            harness.wait_for(lambda: windows()["urgent-a"][1] == "1", processes, "a focused")
            msg("move_left")
            subprocess.run([probe, "--activate", "urgent-b"], env=env, check=True, timeout=5,
                           stdout=subprocess.DEVNULL)
            harness.wait_for(lambda: windows()["urgent-b"][1] == "1", processes, "b focused")
            harness.wait_for(lambda: int(windows()["urgent-a"][4]) + 400 <
                             int(windows()["urgent-b"][4]) and
                             near(edge("urgent-b", False), FOCUSED) and
                             near(edge("urgent-a", False), INACTIVE), processes,
                             "a left and inactive, b right and focused",
                             detail=lambda: f"{windows()}, {edge('urgent-a', False)}, "
                             f"{edge('urgent-b', False)}")

            ask(a)
            harness.wait_for(lambda: urgent_count() == 1, processes, "a urgent")
            seen = []
            start_time = time.monotonic()
            while time.monotonic() - start_time < 1.5:
                seen.append(edge("urgent-a", False))
                time.sleep(.03)
            brightness = [sum(p) for p in seen]
            assert max(brightness) - min(brightness) > 60, f"the border does not pulse: {seen}"
            assert not any(near(p, INACTIVE) for p in seen[3:]), seen
            time.sleep(3.2)  # the pulse lasts four seconds
            held = [edge("urgent-a", False) for _ in range(3)]
            assert all(near(p, URGENT) for p in held), f"urgent color not held: {held}"
            # Focus ends it: the focus color, and the inactive color once focus moves on.
            msg("focus_urgent")
            harness.wait_for(lambda: near(edge("urgent-a", False), FOCUSED) and
                             near(edge("urgent-b", False), INACTIVE), processes,
                             "a focused, b inactive",
                             detail=lambda: f"{edge('urgent-a', False)}, {edge('urgent-b', False)}")

            # Without a border_width the urgent frame sits inside the window's edge, and
            # nothing is drawn once it is over.
            config.write_text(settings(0))
            msg("reload")
            harness.wait_for(lambda: "Configuration reloaded" in log.read_text(), processes, "reload")
            ask(b)
            harness.wait_for(lambda: urgent_count() == 1, processes, "b urgent")
            time.sleep(4.4)
            inside = edge("urgent-b", True)
            assert near(inside, URGENT), f"inset frame missing: {inside}"
            before = windows()["urgent-b"][4:8]
            msg("focus_urgent")
            harness.wait_for(lambda: not near(edge("urgent-b", True), URGENT), processes,
                             "the inset frame gone", detail=lambda: edge("urgent-b", True))
            assert windows()["urgent-b"][4:8] == before, "the frame moved the window"

            # Animations off: the urgent color at once, no pulse.
            config.write_text(settings(3, "false"))
            msg("reload")
            harness.wait_for(lambda: log.read_text().count("Configuration reloaded") == 2,
                             processes, "second reload")
            ask(a)
            harness.wait_for(lambda: urgent_count() == 1, processes, "a urgent without animations")
            harness.wait_for(lambda: near(edge("urgent-a", False), URGENT), processes,
                             "the urgent color", detail=lambda: edge("urgent-a", False))
            for _ in range(5):
                assert near(edge("urgent-a", False), URGENT), edge("urgent-a", False)
                time.sleep(.1)

            for client in (a, b):
                client.stdin.close()
            for process in processes[1:]:
                process.terminate()
                process.wait(timeout=5)
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Urgent borders pulse, hold the urgent color, sit inside without a border, "
                  "and give way to focus")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
