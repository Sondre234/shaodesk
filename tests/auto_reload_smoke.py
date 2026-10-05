# SPDX-License-Identifier: GPL-3.0-or-later
"""Saving the configuration reloads it: written in place or renamed over, once per save, and
not at all with auto_reload = false or for files that are not Lua (or an XKB keymap). A file
with an error, at startup or on a save, gives the default configuration until a save fixes
it."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from harness import wait_for

compositor = str(Path(sys.argv[1]).resolve())
KEYMAP = """xkb_keymap {
  xkb_keycodes { include "evdev" };
  xkb_types { include "complete" };
  xkb_compat { include "complete" };
  xkb_symbols { include "pc+us" };
};
"""

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
            # An XKB keymap beside it counts too (saved here while the error stands, it reloads
            # into the same error): saving keyboard.file reloads, a broken one gives the default
            # configuration, and saving it fixed brings the configuration back.
            keymap = root / "keymap.xkb"
            keymap.write_text(KEYMAP)
            wait_for(lambda: reloads() == 5 and errors() == 3, [process], "reload after an .xkb")
            config.write_text('return { xwayland = false, keyboard = { file = "keymap.xkb" } }')
            wait_for(lambda: reloads() == 6, [process], "reload naming the keymap file")
            keymap.write_text(KEYMAP.replace("pc+us", "pc+no"))
            wait_for(lambda: reloads() == 7, [process], "reload after saving the keymap")
            keymap.write_text("xkb_keymap { oops };\n")
            wait_for(lambda: errors() == 4 and reloads() == 8, [process],
                     "error from a broken keymap")
            assert "init.lua:1: keyboard.file: " in log.read_text()
            keymap.write_text(KEYMAP)
            wait_for(lambda: reloads() == 9, [process], "reload after fixing the keymap")
            settle()
            assert errors() == 4 and reloads() == 9, (errors(), reloads())
            # Turned off, saving changes nothing until the next manual reload.
            config.write_text("return { xwayland = false, auto_reload = false }")
            wait_for(lambda: reloads() == 10, [process], "reload that turns it off")
            config.write_text("return { xwayland = false, auto_reload = false, layout = { gap = 5 } }")
            keymap.write_text(KEYMAP)
            settle()
            assert reloads() == 10, "reloaded with auto_reload = false"
            assert errors() == 4
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
