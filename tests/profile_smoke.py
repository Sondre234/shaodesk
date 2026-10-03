# SPDX-License-Identifier: GPL-3.0-or-later
"""`shaodesk msg profile NAME|next|prev` saves the chosen appearance profile and reloads with it;
an unknown name is refused, and a restart keeps the choice."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    profile = "dark",
    profiles = {
        dark = {},
        light = { appearance = { background = "#f2f4f8" }, shell = { accent = "#3366cc" } },
        sepia = { windows = { border_color = "#704214" } },
    },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-profile-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(CONFIG)
    state = root / "state"
    saved = state / "shaodesk" / "profile"
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=str(state),
               WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout.strip() if ok else result.stderr

    def start(processes):
        output = log.open("w")
        env.pop("SHAODESK_SOCKET", None)
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes[:] = [process for process in processes if process.poll() is None] + [server]
        harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                         "startup")
        env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", log.read_text())[1]
        return server

    def reloads():
        return re.findall(r"Configuration reloaded: .*?(?: \(profile (\S+)\))?$",
                          log.read_text(), re.M)

    def picks(*words, expect):
        before = len(reloads())
        msg("profile", *words)
        harness.wait_for(lambda: len(reloads()) > before, processes, "reload")
        assert reloads()[-1] == expect, (words, reloads())
        assert saved.read_text() == expect + "\n", saved.read_text()

    processes = []
    try:
        server = start(processes)
        assert not saved.exists()
        picks("light", expect="light")
        picks("next", expect="sepia")
        picks("next", expect="dark")  # wraps, in name order
        picks("prev", expect="sepia")
        assert "no profile nope" in msg("profile", "nope", ok=False)
        assert "takes one profile name" in msg("profile", ok=False)
        assert "takes one profile name" in msg("profile", "a", "b", ok=False)
        assert saved.read_text() == "sepia\n"
        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()

        # A restart starts with the saved profile, not the configuration's `profile`.
        server = start(processes)
        msg("reload")
        harness.wait_for(lambda: reloads(), processes, "reload")
        assert reloads()[-1] == "sepia", reloads()
        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()

        # Without profiles there is nothing to pick.
        init.write_text("return { xwayland = false }")
        server = start(processes)
        assert "has no profiles" in msg("profile", "next", ok=False)
        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()
        print("profiles passed")
    except Exception:
        print(log.read_text(), file=sys.stderr)
        raise
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
