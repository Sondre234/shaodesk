# SPDX-License-Identifier: GPL-3.0-or-later
"""Several keyboard layouts: switch_layout moves every keyboard to the next, the previous or a
numbered one, from the control socket and from a binding; virtual keyboards keep theirs; and a
reload keeps the active layout by name, else by place."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import harness

compositor, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

LEFTMETA, LEFTALT, SPACE = 125, 56, 57  # evdev key codes


def config(layout, variant):
    return """return {
    xwayland = false,
    keyboard = { layout = "%s", variant = "%s" },
    bindings = { { mods = { "Super", "Alt" }, key = "space", action = "switch_layout" } },
}""" % (layout, variant)


with tempfile.TemporaryDirectory(prefix="shaodesk-layout-test-") as directory:
    root = Path(directory)
    init = root / "init.lua"
    init.write_text(config("us,no", ",nodeadkeys"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def run(*words):
        return subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                              text=True, timeout=30)

    def msg(*words):
        result = run(*words)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def keyboard():
        """The layouts as (short, name, active), and each keyboard's layout by name."""
        layouts, keyboards = [], {}
        for line in msg("get", "keyboard").splitlines():
            fields = line.split("\t")
            if fields[0] == "layout":
                layouts.append((fields[3], fields[4], fields[2] == "1"))
            elif fields[0] == "keyboard":
                keyboards[fields[6]] = int(fields[1])
        return layouts, keyboards

    def active():
        """The active layout's number, and every keyboard's, by name."""
        layouts, keyboards = keyboard()
        return next(i + 1 for i, layout in enumerate(layouts) if layout[2]), keyboards

    def key(code, state):
        msg("headless_keyboard", "key", "one", str(code), state)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
    processes = [server]
    try:
        harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                         "startup")
        text = log.read_text()
        env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
        msg("headless_keyboard", "add", "one")
        msg("headless_keyboard", "add", "two")
        assert keyboard() == ([("us", "English (US)", True),
                               ("no", "Norwegian (no dead keys)", False)],
                              {"one": 1, "two": 1}), keyboard()

        # From the control socket: next and prev wrap, a number picks one.
        msg("switch_layout")
        assert active() == (2, {"one": 2, "two": 2}), active()
        msg("switch_layout", "next")
        assert active() == (1, {"one": 1, "two": 1}), active()
        msg("switch_layout", "prev")
        assert active() == (2, {"one": 2, "two": 2}), active()
        msg("switch_layout", "1")
        assert active() == (1, {"one": 1, "two": 1}), active()
        msg("switch_layout", "1")
        assert active() == (1, {"one": 1, "two": 1}), active()
        for words in (("3",), ("0",), ("-1",), ("sideways",), ("next", "next")):
            result = run("switch_layout", *words)
            assert result.returncode != 0, words
        result = run("switch_layout", "3")
        assert "the keymap has 2 layouts" in result.stdout + result.stderr, result
        assert active() == (1, {"one": 1, "two": 1}), active()

        # From a binding, typed on one keyboard: both switch.
        key(LEFTMETA, "press")
        key(LEFTALT, "press")
        key(SPACE, "press")
        key(SPACE, "release")
        key(LEFTALT, "release")
        key(LEFTMETA, "release")
        assert active() == (2, {"one": 2, "two": 2}), active()

        # A virtual keyboard (from the pointer probe) keeps its own keymap and layout.
        pointer = subprocess.Popen([pointer_probe, "1280", "720"], env=env, text=True,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        processes.append(pointer)
        assert pointer.stdout.readline().strip() == "ready"
        pointer.stdin.write("key alt down\n")
        pointer.stdin.flush()
        assert pointer.stdout.readline().strip() == "done"
        harness.wait_for(lambda: len(active()[1]) == 3, processes, "a virtual keyboard")
        virtual = next(name for name in active()[1] if name not in ("one", "two"))
        msg("switch_layout", "next")
        assert active() == (1, {"one": 1, "two": 1, virtual: 1}), active()
        msg("switch_layout", "next")
        assert active() == (2, {"one": 2, "two": 2, virtual: 1}), active()
        pointer.stdin.close()
        assert pointer.wait(timeout=30) == 0
        processes.remove(pointer)

        # A reload keeps the active layout: with another variant, by place...
        init.write_text(config("us,no", ","))
        msg("reload")
        assert keyboard() == ([("us", "English (US)", False), ("no", "Norwegian", True)],
                              {"one": 2, "two": 2}), keyboard()
        # ...and by name when the layouts change places: Norwegian, now first.
        init.write_text(config("no,us", ","))
        msg("reload")
        assert keyboard() == ([("no", "Norwegian", True), ("us", "English (US)", False)],
                              {"one": 1, "two": 1}), keyboard()
        # A reload that leaves the keymap as it was leaves the layout too.
        msg("switch_layout", "2")
        msg("reload")
        assert active() == (2, {"one": 2, "two": 2}), active()
        # And a keyboard plugged in now types in it too.
        msg("headless_keyboard", "add", "three")
        assert active() == (2, {"one": 2, "two": 2, "three": 2}), active()
        # With one layout left, the first.
        init.write_text(config("us", ""))
        msg("reload")
        assert keyboard() == ([("us", "English (US)", True)],
                              {"one": 1, "two": 1, "three": 1}), keyboard()
        msg("switch_layout")
        assert active() == (1, {"one": 1, "two": 1, "three": 1}), active()

        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()
        print("Switching keyboard layouts, virtual keyboards, and reloads passed")
    except Exception:
        print(log.read_text(), file=sys.stderr)
        raise
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
