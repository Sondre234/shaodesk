# SPDX-License-Identifier: GPL-3.0-or-later
"""Keymaps reach keyboards and, through the seat, applications: a keyboard gets the keymap of
keyboard.layout or keyboard.file, a reload swaps it while keys stay held and locks stay on, a
virtual keyboard keeps its own, and a keymap file that does not compile is a configuration
error, at startup or on a reload, which leaves the default configuration's keymap in place of
none. `get keyboard` says where the keymap came from and what each keyboard is doing."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, pointer_probe, default_config = (str(Path(p).resolve()) for p in sys.argv[1:5])

# Two layouts, the first renamed so it is plain where the keymap came from.
KEYMAP = """xkb_keymap {
  xkb_keycodes { include "evdev" };
  xkb_types { include "complete" };
  xkb_compat { include "complete" };
  xkb_symbols { include "pc+us+no:2" name[Group1] = "Testish"; };
};
"""
# The same with a syntax error on line 5.
BROKEN = KEYMAP.replace('name[Group1] = "Testish";', "oops")
SHIFT, CAPS_LOCK, A = 42, 58, 30  # evdev key codes
SH_SHIFT, SH_CAPS, SH_ALT = 1, 2, 8  # modifier bits in `get keyboard`


def config(keyboard):
    """A configuration whose keyboard table, on line 3, holds `keyboard`."""
    return "return {\n    xwayland = false,\n    keyboard = { %s },\n}\n" % keyboard


with tempfile.TemporaryDirectory(prefix="shaodesk-keymap-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    (root / "keymap.xkb").write_text(KEYMAP)
    (root / "broken.xkb").write_text(BROKEN)
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               SHAODESK_DEFAULT_CONFIG=default_config)
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def layouts(expected=True):
        """The names of the layouts in the keymap a new application gets."""
        result = subprocess.run([probe, "--keymap"], env=env, capture_output=True, text=True,
                                timeout=30)
        assert (result.returncode == 0) == expected, (result.stdout, result.stderr)
        symbols = result.stdout[result.stdout.find("xkb_symbols"):]
        return re.findall(r'^\s*name\[\w*?(\d+)\]\s*=\s*"([^"]*)";', symbols, re.M)

    def keyboard():
        """`get keyboard`: the source, the layouts as (short, name, active), and the keyboards
        by name as (layout, layouts, virtual, held, locked)."""
        source, layouts, keyboards = None, [], {}
        for line in msg("get", "keyboard").splitlines():
            fields = line.split("\t")
            if fields[0] == "source":
                source = fields[1:]
            elif fields[0] == "layout":
                assert int(fields[1]) == len(layouts) + 1, line
                layouts.append((fields[3], fields[4], fields[2] == "1"))
            elif fields[0] == "keyboard":
                keyboards[fields[6]] = tuple(int(field) for field in fields[1:6])
        return source, layouts, keyboards

    def key(name, code, state):
        msg("headless_keyboard", "key", name, str(code), state)

    processes = []

    def start(text):
        init.write_text(text)
        with log.open("w") as output:
            server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                      env=env, stdout=output, stderr=output)
        processes.append(server)
        harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                         "startup")
        text = log.read_text()
        env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
        return server

    def stop(server):
        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()
        processes.remove(server)

    try:
        # --check-config refuses a broken keymap file, saying where in both files.
        init.write_text(config('file = "broken.xkb"'))
        check = subprocess.run([compositor, "--check-config", "--config", str(init)], env=env,
                               capture_output=True, text=True, timeout=30)
        assert check.returncode != 0, check.stdout
        assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in check.stderr, check.stderr

        server = start(config('layout = "us,no"'))
        # Until a keyboard is plugged in, applications get no keymap from the seat.
        layouts(expected=False)
        assert keyboard() == (["rules"], [("us", "English (US)", True), ("no", "Norwegian", False)],
                              {}), keyboard()
        msg("headless_keyboard", "add", "one")
        assert layouts() == [("1", "English (US)"), ("2", "Norwegian")], layouts()
        assert keyboard()[2] == {"one": (1, 2, 0, 0, 0)}, keyboard()
        # The control socket refuses what it cannot do.
        for words in (("add", "one"), ("remove", "nobody"), ("key", "one", "30", "hold"),
                      ("key", "one", "99999", "press"), ("jump",)):
            result = subprocess.run([compositor, "msg", "headless_keyboard", *words], env=env,
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode != 0, words

        # A virtual keyboard (from the pointer probe) brings its own keymap, with one layout.
        pointer = subprocess.Popen([pointer_probe, "1280", "720"], env=env, text=True,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        processes.append(pointer)
        assert pointer.stdout.readline().strip() == "ready"

        def tell(line):
            pointer.stdin.write(line + "\n")
            pointer.stdin.flush()
            assert pointer.stdout.readline().strip() == "done"

        tell("key alt down")
        harness.wait_for(lambda: len(keyboard()[2]) == 2, processes, "a virtual keyboard")
        virtual = next(name for name, state in keyboard()[2].items() if state[2])
        assert keyboard()[2][virtual] == (1, 1, 1, SH_ALT, 0), keyboard()

        # keyboard.file, relative to the configuration, replaces the names on a reload, while a
        # key is held and Caps Lock is on; the virtual keyboard keeps its keymap and its Alt.
        key("one", CAPS_LOCK, "press")
        key("one", CAPS_LOCK, "release")
        key("one", SHIFT, "press")
        assert keyboard()[2]["one"] == (1, 2, 0, SH_SHIFT, SH_CAPS), keyboard()
        init.write_text(config('layout = "de", file = "keymap.xkb"'))
        msg("reload")
        assert layouts() == [("1", "Testish"), ("2", "Norwegian")], layouts()
        source, names, keyboards = keyboard()
        assert source == ["file", str(root / "keymap.xkb")], source
        assert names == [("te", "Testish", True), ("no", "Norwegian", False)], names
        assert keyboards == {"one": (1, 2, 0, SH_SHIFT, SH_CAPS), virtual: (1, 1, 1, SH_ALT, 0)}, \
            keyboards
        key("one", SHIFT, "release")
        key("one", CAPS_LOCK, "press")
        key("one", CAPS_LOCK, "release")
        assert keyboard()[2]["one"] == (1, 2, 0, 0, 0), keyboard()
        tell("key alt up")
        pointer.stdin.close()
        assert pointer.wait(timeout=30) == 0
        processes.remove(pointer)
        harness.wait_for(lambda: len(keyboard()[2]) == 1, processes, "the virtual keyboard gone")

        # Back to names, and a keyboard plugged in later gets the same.
        init.write_text(config('layout = "no"'))
        msg("reload")
        assert layouts() == [("1", "Norwegian")], layouts()
        msg("headless_keyboard", "add", "two")
        msg("headless_keyboard", "remove", "one")
        assert layouts() == [("1", "Norwegian")], layouts()
        assert keyboard() == (["rules"], [("no", "Norwegian", True)], {"two": (1, 1, 0, 0, 0)})

        # A broken file on a reload: the default configuration, and a keyboard that still works.
        init.write_text(config('layout = "no", file = "broken.xkb"'))
        msg("reload")
        harness.wait_for(lambda: "using the default configuration" in log.read_text(), processes,
                         "the error reported")
        assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in log.read_text()
        assert layouts() == [("1", "English (US)")], layouts()
        assert keyboard()[:2] == (["rules"], [("us", "English (US)", True)]), keyboard()
        key("two", A, "press")
        key("two", A, "release")
        stop(server)

        # The same at startup.
        server = start(config('file = "broken.xkb"'))
        assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in log.read_text()
        msg("headless_keyboard", "add", "one")
        assert layouts() == [("1", "English (US)")], layouts()
        stop(server)
        print("Keymaps from names and files, reloads, virtual keyboards, and broken keymap "
              "files passed")
    except Exception:
        print(log.read_text(), file=sys.stderr)
        raise
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
