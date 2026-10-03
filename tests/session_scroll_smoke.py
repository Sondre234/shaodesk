# SPDX-License-Identifier: GPL-3.0-or-later
"""A saved session keeps the columns of the scrolling layout: which tiles share a column, in
what order, and the width of each column."""
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
               scroll = { follow = "edge", width = 0.5, step = 0.1 } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-session-scroll-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    state = root / "state"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=str(state),
               WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(focused=r[1] == "1", tiled=r[3] == "1", x=int(r[4]), y=int(r[5]),
                           width=int(r[6]), height=int(r[7])) for r in rows}

    def shape():
        """Widths, heights and the arrangement relative to window A, whatever the view shows."""
        w = windows()
        return {t: (v["x"] - w["A"]["x"], v["y"], v["width"], v["height"]) for t, v in w.items()}

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=5, stdout=subprocess.DEVNULL)
        wait_for(lambda: windows()[title]["focused"], f"{title} focused")

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
                wait_for(lambda: title in windows() and windows()[title]["focused"],
                         f"{title} focused")
            wait_for(lambda: all(w["tiled"] for w in windows().values()), "all tiled")

            # Columns: [A] [B over C]; A is wider than the others.
            focus("C")
            msg("consume_left")
            focus("A")
            msg("column_widen")
            wait_for(lambda: windows()["A"]["width"] == 768, "A widened")
            wait_for(lambda: windows()["B"]["x"] == windows()["C"]["x"] and
                     windows()["C"]["y"] > windows()["B"]["y"], "C stacked under B")
            saved = shape()
            assert saved["B"][3] == saved["C"][3] == 360 and saved["B"][2] == 640, saved

            out = msg("session", "save", "columns")
            assert "saved columns: 3 windows" in out, out
            text = (state / "shaodesk" / "sessions" / "columns").read_text()
            layout_line = [l for l in text.splitlines() if l.startswith("layout\t")]
            assert len(layout_line) == 1 and layout_line[0].endswith("\t0.6000,0.5000"), text

            # Scramble: C leaves its stack, A gets another width, B is widened.
            focus("C")
            msg("expel")
            focus("A")
            msg("column_cycle_width")
            focus("B")
            msg("column_widen")
            wait_for(lambda: shape() != saved, "arrangement changed")
            assert shape()["C"][0] != shape()["B"][0]

            out = msg("session", "restore", "columns")
            assert "restored 3, launched 0, not found 0" in out, out
            wait_for(lambda: shape() == saved, "columns restored")
            assert msg("session", "delete", "columns") == ""
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("session scroll columns passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
