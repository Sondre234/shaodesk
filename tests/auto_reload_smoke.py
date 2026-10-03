# SPDX-License-Identifier: GPL-3.0-or-later
"""Saving the configuration reloads it: written in place or renamed over, once per save, and
not at all with auto_reload = false or for files that are not Lua. A file with an error, at
startup or on a save, gives the default configuration until a save fixes it."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from harness import wait_for

compositor = str(Path(sys.argv[1]).resolve())

with tempfile.TemporaryDirectory(prefix="shaodesk-auto-reload-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text("return { xwayland = false, layout = { gap = -1 } }")
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               SHAODESK_AUTO_RELOAD="1", SHAODESK_DEFAULT_CONFIG=str(Path(sys.argv[2]).resolve()))
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def reloads():
        return log.read_text().count("Configuration reloaded")

    def errors():
        return log.read_text().count("using the default configuration")

    def settle():
        time.sleep(0.6)

    with log.open("w") as output:
        process = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                   env=env, stdout=output, stderr=output)
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), [process], "startup")
            assert errors() == 1 and "layout.gap must be between" in log.read_text()
            # Written in place, and fixed.
            config.write_text("return { xwayland = false, layout = { gap = 3 } }")
            wait_for(lambda: reloads() == 1, [process], "reload after writing")
            # Saved through a temporary file renamed over it, as many editors do: one reload.
            temporary = root / "init.lua.tmp"
            temporary.write_text("return { xwayland = false, layout = { gap = 4 } }")
            temporary.rename(config)
            wait_for(lambda: reloads() == 2, [process], "reload after renaming")
            settle()
            assert reloads() == 2, "one save reloaded more than once"
            # Another Lua file beside it, such as theme.lua, counts; other files do not.
            (root / "notes.txt").write_text("not configuration")
            settle()
            assert reloads() == 2, "a file that is not Lua reloaded"
            (root / "theme.lua").write_text("return {}")
            wait_for(lambda: reloads() == 3, [process], "reload after writing theme.lua")
            # An error on a save gives the default configuration again.
            config.write_text("return { xwayland = false, layout = { bogus = 1 } }")
            wait_for(lambda: errors() == 2, [process], "error on a save")
            assert reloads() == 4
            # Turned off, saving changes nothing until the next manual reload.
            config.write_text("return { xwayland = false, auto_reload = false }")
            wait_for(lambda: reloads() == 5, [process], "reload that turns it off")
            config.write_text("return { xwayland = false, auto_reload = false, layout = { gap = 5 } }")
            settle()
            assert reloads() == 5, "reloaded with auto_reload = false"
            assert errors() == 2
            process.terminate()
            assert process.wait(timeout=30) == 0, log.read_text()
            print("Automatic reload on save passed")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
