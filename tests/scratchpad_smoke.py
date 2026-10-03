# SPDX-License-Identifier: GPL-3.0-or-later
"""The scratchpad, as in sway: move_to_scratchpad hides a window, scratchpad_show brings one to
the middle of the focused output, hides it again, and cycles through them; moving a shown one
to a workspace or tiling it takes it out; features.scratchpad = false turns the actions off and
returns hidden windows on reload."""
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
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { scratchpad = %s },
}"""

with tempfile.TemporaryDirectory(prefix="shaode-scratchpad-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG % "true")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        """By title: workspace, focused, minimized, tiled, x, y, width, height, visible,
        scratchpad."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", minimized=r[2] == "1",
                           tiled=r[3] == "1", x=int(r[4]), y=int(r[5]), width=int(r[6]),
                           height=int(r[7]), visible=r[11] == "1", scratchpad=r[12] == "1")
                for r in rows}

    def used():
        return msg("get", "workspaces").split("\t")[3]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def hidden(name):
        w = windows()[name]
        return w["scratchpad"] and w["minimized"] and not w["visible"] and not w["focused"]

    def shown(name):
        """Visible, focused, floating, and centred on the output."""
        w = windows()[name]
        return (w["scratchpad"] and w["visible"] and w["focused"] and not w["minimized"] and
                not w["tiled"] and abs(w["x"] + w["width"] / 2 - 640) <= 2 and
                abs(w["y"] + w["height"] / 2 - 360) <= 2)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODE_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            for title in ("A", "B"):
                processes.append(subprocess.Popen([probe, "--window-only"],
                                                  env=dict(env, SHAODE_PROBE_TITLE=title),
                                                  stdout=subprocess.DEVNULL))
                wait_for(lambda: title in windows() and windows()[title]["focused"],
                         f"{title} mapped")
            wait_for(lambda: all(w["tiled"] for w in windows().values()), "A and B tiled")

            # Nothing in the scratchpad: showing it does nothing.
            before = windows()
            msg("scratchpad_show")
            assert windows() == before, windows()

            # B leaves the tiling for the scratchpad; A takes the whole output and the focus.
            msg("move_to_scratchpad")
            wait_for(lambda: hidden("B") and not windows()["B"]["tiled"], "B hidden")
            wait_for(lambda: windows()["A"]["focused"] and windows()["A"]["width"] > 1100,
                     "A fills the output")
            assert used() == "1", used()

            # Shown floating in the middle, then hidden again.
            msg("scratchpad_show")
            wait_for(lambda: shown("B"), "B shown")
            assert windows()["A"]["tiled"], windows()
            msg("scratchpad_show")
            wait_for(lambda: hidden("B") and windows()["A"]["focused"], "B hidden again")

            # With A in the scratchpad as well, the workspace is empty and presses cycle.
            msg("move_to_scratchpad")
            wait_for(lambda: hidden("A") and hidden("B"), "both hidden")
            assert used() == "-", used()
            msg("scratchpad_show")
            wait_for(lambda: shown("B") and hidden("A"), "B shown first")
            msg("scratchpad_show")
            wait_for(lambda: hidden("B") and hidden("A"), "B hidden")
            msg("scratchpad_show")
            wait_for(lambda: shown("A") and hidden("B"), "then A")
            msg("scratchpad_show")
            wait_for(lambda: hidden("A") and hidden("B"), "A hidden")
            msg("scratchpad_show")
            wait_for(lambda: shown("B") and hidden("A"), "B again")

            # On another workspace a hidden window comes before one shown elsewhere.
            msg("workspace", "2")
            msg("scratchpad_show")
            wait_for(lambda: shown("A") and windows()["A"]["workspace"] == 2, "A on workspace 2")
            assert not windows()["B"]["minimized"] and windows()["B"]["workspace"] == 1, windows()
            # Moved to a workspace, it leaves the scratchpad and stays floating.
            msg("move_to_workspace", "3")
            wait_for(lambda: not windows()["A"]["scratchpad"] and
                     windows()["A"]["workspace"] == 3 and not windows()["A"]["tiled"],
                     "A out of the scratchpad on workspace 3")
            # With none hidden, the one shown on workspace 1 comes over.
            msg("workspace", "3")
            msg("scratchpad_show")
            wait_for(lambda: shown("B") and windows()["B"]["workspace"] == 3, "B on workspace 3")
            # Tiled, it leaves the scratchpad.
            msg("toggle_floating")
            wait_for(lambda: windows()["B"]["tiled"] and not windows()["B"]["scratchpad"],
                     "B tiled and out of the scratchpad")
            before = windows()
            msg("scratchpad_show")
            assert windows() == before, windows()

            # Turned off, the actions do nothing, and a reload returns hidden windows.
            msg("move_to_scratchpad")
            wait_for(lambda: hidden("B"), "B hidden before turning the scratchpad off")
            init.write_text(CONFIG % "false")
            msg("workspace", "1")
            msg("reload")
            wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
            wait_for(lambda: not windows()["B"]["scratchpad"] and windows()["B"]["visible"] and
                     not windows()["B"]["minimized"] and windows()["B"]["workspace"] == 1,
                     "B back on workspace 1")
            focused = [t for t, w in windows().items() if w["focused"]]
            msg("move_to_scratchpad")
            msg("scratchpad_show")
            assert not any(w["scratchpad"] or w["minimized"] for w in windows().values()), \
                windows()
            assert [t for t, w in windows().items() if w["focused"]] == focused, windows()
            assert log.read_text().count("features.scratchpad is false") == 1, "not logged once"

            for window in processes[1:]:
                window.kill()
                window.wait(timeout=5)
            del processes[1:]
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Scratchpad hid, showed, cycled, released, and turned off")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
