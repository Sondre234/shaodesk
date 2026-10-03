# SPDX-License-Identifier: GPL-3.0-or-later
"""A click on a panel keeps the keyboard where it is, even on another monitor than the focused
window's: the taskbar must see that window as focused to minimize it on a click, as Windows does.
A click on the bare desktop there still takes the focus."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""
LAYOUT = (2080, 720)

with tempfile.TemporaryDirectory(prefix="shaodesk-panel-focus-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               WLR_HEADLESS_OUTPUTS="2")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def windows():
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(focused=r[1] == "1", output=r[10]) for r in rows}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            virtual = subprocess.Popen([pointer_probe, str(LAYOUT[0]), str(LAYOUT[1])], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(virtual)
            assert virtual.stdout.readline().strip() == "ready"

            def pointer(*commands):
                virtual.stdin.write(" ".join(commands) + "\n")
                virtual.stdin.flush()
                assert virtual.stdout.readline().strip() == "done"

            # The probe's window opens on the first monitor and its 48 pixel bottom panel on the
            # second.
            processes.append(subprocess.Popen(
                [probe, "--external-control"], env=dict(env, SHAODESK_PROBE_TITLE="A"),
                stdout=subprocess.DEVNULL))
            wait_for(lambda: focused() == "A", "A focused")
            assert windows()["A"]["output"] == "HEADLESS-1", windows()

            pointer("move", str(1280 + 400), str(600 - 20), "click", "left")
            # The panel takes no input, so nothing tells when the click is through but a later
            # request on the same connection; the control socket answers after it.
            msg("get", "windows")
            assert focused() == "A", f"a click on the panel took the focus: {windows()}"

            pointer("move", str(1280 + 400), str(200), "click", "left")
            wait_for(lambda: focused() is None, "a click on the bare desktop takes the focus")
            print("Panel focus passed")
        finally:
            for process in reversed(processes):
                process.terminate()
                if process.stdin:
                    process.stdin.close()
                process.wait(timeout=30)
