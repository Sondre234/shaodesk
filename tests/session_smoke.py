# SPDX-License-Identifier: GPL-3.0-or-later
"""session save / restore / list / delete: a saved arrangement of windows (workspaces, tiled or
floating state, layouts, the current workspace) is put back by matching windows on app ID and
title, and `restore NAME launch` starts an application that is gone and places its new window."""
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
    layout = { tiling = true, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-session-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    state = root / "state"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=str(state),
               WLR_RENDERER="pixman", SHAODE_PROBE_TITLE="Relaunched",
               SHAODE_PROBE_APP_ID="app-b")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def windows():
        """By title: workspace, focused, minimized, tiled, x, y, width, height."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", tiled=r[3] == "1",
                           x=int(r[4]), y=int(r[5]), width=int(r[6]), height=int(r[7]))
                for r in rows}

    def summary():
        return {t: (w["workspace"], w["tiled"]) for t, w in windows().items()}

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        clients = {}
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]

            assert "usage" in msg("session", ok=False)
            assert "no session named" in msg("session", "restore", "nothing", ok=False)
            assert "session name" in msg("session", "save", "../evil", ok=False)
            assert "session name" in msg("session", "save", "x.new", ok=False)
            assert msg("session", "list") == ""

            for title in ("A", "B", "C"):
                clients[title] = subprocess.Popen([probe, "--window-only"],
                                                  env=dict(env, SHAODE_PROBE_TITLE=title,
                                                      SHAODE_PROBE_APP_ID=f"app-{title.lower()}"),
                                                  stdout=subprocess.DEVNULL)
                processes.append(clients[title])
                wait_for(lambda: title in windows() and windows()[title]["focused"],
                         f"{title} focused")
            wait_for(lambda: all(w["tiled"] for w in windows().values()), "all tiled")

            # B goes to workspace 2, C floats, A stays; workspace 1 uses the master layout.
            msg("toggle_floating")  # C, focused
            wait_for(lambda: not windows()["C"]["tiled"], "C floats")
            msg("layout_master")
            focus("B")
            wait_for(lambda: windows()["B"]["focused"], "B focused")
            msg("move_to_workspace", "2")
            wait_for(lambda: windows()["B"]["workspace"] == 2, "B on workspace 2")
            msg("workspace", "3")
            saved_state = summary()
            assert saved_state == {"A": (1, True), "B": (2, True), "C": (1, False)}, saved_state
            saved_c = windows()["C"]
            assert msg("get", "layout").split()[0] == "dwindle"  # workspace 3
            out = msg("session", "save", "work")
            assert "saved work: 3 windows" in out, out

            saved = (state / "shaode" / "sessions" / "work").read_text()
            assert saved.startswith("shaode-session 1\n"), saved
            assert saved.count("\nwindow\t") == 3 and "\nlayout\tHEADLESS-1\t0\t1\t" in saved
            assert "wayland_probe" in saved and "--window-only" in saved, saved
            listing = msg("session", "list").split("\t")
            assert listing[0] == "work" and listing[1] == "3", listing

            # Scramble everything.
            msg("workspace", "1")
            msg("layout_dwindle")
            focus("A")
            wait_for(lambda: windows()["A"]["focused"], "A focused")
            msg("move_to_workspace", "4")
            wait_for(lambda: windows()["A"]["workspace"] == 4, "A moved")
            focus("C")
            wait_for(lambda: windows()["C"]["focused"], "C focused")
            msg("toggle_floating")
            wait_for(lambda: windows()["C"]["tiled"], "C tiles")
            msg("workspace", "4")
            assert summary() != saved_state

            out = msg("session", "restore", "work")
            assert "restored 3, launched 0, not found 0" in out, out
            wait_for(lambda: summary() == saved_state, "windows restored")
            assert msg("get", "workspace").strip() == "3"
            msg("workspace", "1")
            assert msg("get", "layout").split()[0] == "master"
            after = windows()["C"]
            assert (after["x"], after["y"], after["width"], after["height"]) == \
                (saved_c["x"], saved_c["y"], saved_c["width"], saved_c["height"]), (saved_c, after)

            # A window that is gone is not restored, or launched without `launch`.
            clients["B"].kill()
            clients["B"].wait(timeout=30)
            processes.remove(clients["B"])
            wait_for(lambda: "B" not in windows(), "B closed")
            out = msg("session", "restore", "work")
            assert "restored 2, launched 0, not found 1" in out, out
            assert "B" not in windows() and "Relaunched" not in windows()

            # With `launch` its command runs again and the new window takes B's place.
            out = msg("session", "restore", "work", "launch")
            assert "restored 2, launched 1, not found 0" in out, out
            wait_for(lambda: "Relaunched" in windows(), "B launched again")
            wait_for(lambda: windows()["Relaunched"]["workspace"] == 2 and
                     windows()["Relaunched"]["tiled"], "launched window placed")

            assert msg("session", "delete", "work") == ""
            assert msg("session", "list") == ""
            assert "no session named" in msg("session", "delete", "work", ok=False)

            # A window's title and app ID are its own to choose: tabs, newlines and percent signs
            # must not add records to the file or make it unreadable.
            hostile = subprocess.Popen(
                [probe, "--window-only"],
                env=dict(env, SHAODE_PROBE_TITLE="T\t%41\nwindow\tforged\t1",
                         SHAODE_PROBE_APP_ID="odd id%zz\n"),
                stdout=subprocess.DEVNULL)
            processes.append(hostile)
            wait_for(lambda: any(t.startswith("T ") for t in windows()), "the odd window mapped")
            count = len(windows())
            out = msg("session", "save", "odd")
            assert f"saved odd: {count} windows" in out, out
            lines = (state / "shaode" / "sessions" / "odd").read_text().split("\n")[:-1]
            kinds = [line.split("\t")[0] for line in lines[1:]]
            assert set(kinds) <= {"output", "layout", "window"}, kinds
            assert kinds.count("window") == count, (kinds, count)
            assert not any(line.startswith("window\tforged") for line in lines), lines
            out = msg("session", "restore", "odd")
            assert f"restored {count}," in out, out
            server.terminate()
            assert server.wait(timeout=30) == 0, log.read_text()
            print("session save and restore passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=30)
