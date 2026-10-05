# SPDX-License-Identifier: GPL-3.0-or-later
"""Starting programs with stand-ins on a PATH the test controls: a program that cannot start is
reported to the caller and, as `spawn-error`, to the shell, whether a key binding or the control
socket asked for it; and the terminal action finds the configured terminal, else $TERMINAL, else
the first installed of a list."""
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile

import harness

compositor, example = (str(Path(p).resolve()) for p in sys.argv[1:3])
LEFTMETA, Q = 125, 16  # evdev key codes

with tempfile.TemporaryDirectory(prefix="shaodesk-launch-") as directory:
    root = Path(directory)
    tools = root / "bin"
    tools.mkdir()
    tool_log = root / "tools.log"
    tool_log.touch()

    def stand_in(name):
        """A program on PATH that records its name and arguments."""
        (tools / name).write_text(f'#!/bin/sh\necho "{name} $*" >> "$TOOL_LOG"\n')
        (tools / name).chmod(0o755)

    def calls():
        return tool_log.read_text().splitlines()

    config = root / "init.lua"
    config.write_text(Path(example).read_text().replace("xwayland = true", "xwayland = false"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman", PATH=str(tools),
               TOOL_LOG=str(tool_log), TERMINAL="my-terminal")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def key(code, state):
        msg("headless_keyboard", "key", "one", str(code), state)

    def press(*codes):
        for code in codes:
            key(code, "press")
        for code in reversed(codes):
            key(code, "release")

    class Subscriber:
        """The control socket's stream, as the shell reads it."""

        def __init__(self):
            self.socket = socket.socket(socket.AF_UNIX)
            self.socket.connect(env["SHAODESK_SOCKET"])
            self.socket.sendall(b"subscribe\n")
            self.socket.settimeout(0.05)
            self.buffer = ""

        def errors(self):
            try:
                while data := self.socket.recv(8192):
                    self.buffer += data.decode()
            except socket.timeout:
                pass
            return re.findall(r"^spawn-error (.*)$", self.buffer, re.M)

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
    processes = [server]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: (calls(), log.read_text()))

    try:
        wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
        text = log.read_text()
        env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
        subscriber = Subscriber()
        msg("headless_keyboard", "add", "one")

        # The control socket: a program that is there starts; one that is not is an error for
        # the caller and for the panel.
        stand_in("editor")
        msg("spawn", "editor", "notes.txt")
        wait_for(lambda: calls() == ["editor notes.txt"], "the editor")
        missing = "no-such-program: No such file or directory"
        assert f"error: cannot launch {missing}" in msg("spawn", "no-such-program", "-v", ok=False)
        wait_for(lambda: subscriber.errors() == [f"Cannot launch {missing}"], "the panel's error")
        assert f"Cannot launch {missing}" in log.read_text()

        # A key binding: Super + Q spawns kitty, which this PATH lacks.
        press(LEFTMETA, Q)
        wait_for(lambda: len(subscriber.errors()) == 2, "Super + Q's error on the panel")
        assert subscriber.errors()[1] == "Cannot launch kitty: No such file or directory", \
            subscriber.errors()

        # The terminal action, with no terminal installed: the error says what to do.
        error = msg("terminal", ok=False)
        assert "error: no terminal installed: set terminal in the configuration, or install one " \
            "of kitty, foot, alacritty, wezterm, ghostty, konsole, gnome-terminal, xterm" in error
        wait_for(lambda: len(subscriber.errors()) == 3, "the missing terminal on the panel")
        assert subscriber.errors()[2].startswith("No terminal installed: "), subscriber.errors()
        assert "$TERMINAL, my-terminal, is not installed" in log.read_text()

        # The first of the list that is installed, then $TERMINAL once it is.
        def opens(expected):
            tool_log.write_text("")
            msg("terminal")
            wait_for(lambda: calls() == [expected], f"{expected} opened")
        stand_in("xterm")
        opens("xterm ")
        stand_in("foot")
        opens("foot ")
        stand_in("my-terminal")
        opens("my-terminal ")

        # The configured terminal wins, with its arguments; one not installed is an error rather
        # than a reason to open another.
        config.write_text(config.read_text().replace(
            "    version = 1,\n", '    version = 1,\n    terminal = { "kitty", "-1" },\n', 1))
        msg("reload")
        assert "error: cannot launch kitty: No such file or directory" in msg("terminal", ok=False)
        stand_in("kitty")
        opens("kitty -1")

        server.terminate()
        assert server.wait(timeout=30) == 0, log.read_text()
        print("Programs and terminals start, and those that cannot are reported")
    except Exception:
        print(log.read_text(), file=sys.stderr)
        raise
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
