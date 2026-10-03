# SPDX-License-Identifier: GPL-3.0-or-later
"""The window switcher lists every window on every output and workspace, most recently focused
first, on the focused output; it moves its selection, focuses the chosen window (switching its
output's workspace), cancels, follows windows closing, and tells subscribers each step. With
wtype installed, Alt+Tab from a virtual keyboard confirms on releasing Alt."""
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    windows = { rules = { { title = "^C$", output = "HEADLESS-2" } } },
    bindings = {
        { mods = { "Alt" }, key = "Tab", action = "switcher" },
        { mods = { "Alt", "Shift" }, key = "Tab", action = "switcher_prev" },
    },
}"""

with tempfile.TemporaryDirectory(prefix="shaodesk-switcher-test-") as directory:
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
        """By title: workspace, focused, minimized, output, visible."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", minimized=r[2] == "1",
                           output=r[10], visible=r[11] == "1") for r in rows}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message, detail=lambda: f"windows: {windows()}")

    class Events:
        """A subscriber's lines other than the state it gets after every change."""

        def __init__(self):
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(env["SHAODESK_SOCKET"])
            self.sock.sendall(b"subscribe\n")
            self.sock.settimeout(0.05)
            self.buffer = b""
            self.lines = []

        def poll(self):
            try:
                while True:
                    data = self.sock.recv(65536)
                    if not data:
                        break
                    self.buffer += data
            except socket.timeout:
                pass
            *complete, self.buffer = self.buffer.split(b"\n")
            self.lines += [line.decode() for line in complete
                           if line.startswith(b"switcher")]

        def expect(self, predicate, message):
            """Waits for the switcher lines to satisfy `predicate`, then forgets them."""
            def seen():
                self.poll()
                return predicate(self.lines)
            harness.wait_for(seen, processes, message, detail=lambda: f"events: {self.lines}")
            lines, self.lines = self.lines, []
            return lines

    def opened(lines):
        """The output, selection, and window titles of the last full switcher announcement."""
        for i in range(len(lines) - 1, -1, -1):
            words = lines[i].split(" ")
            if words[0] == "switcher" and len(lines) - i - 1 >= int(words[3]):
                entries = [line[len("switcher-window "):].split("\t")
                           for line in lines[i + 1:i + 1 + int(words[3])]]
                return words[1], int(words[2]), [e[1] for e in entries], entries
        return None

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(init)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        clients = {}
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            events = Events()

            def open_window(title):
                clients[title] = subprocess.Popen([probe, "--window-only"],
                                                  env=dict(env, SHAODESK_PROBE_TITLE=title),
                                                  stdout=subprocess.DEVNULL)
                processes.append(clients[title])
                wait_for(lambda: title in windows(), f"{title} mapped")

            # A and B on workspace 1 of HEADLESS-1, D on its workspace 2, C on HEADLESS-2.
            open_window("A")
            open_window("B")
            msg("output", "HEADLESS-1", "workspace", "2")
            open_window("D")
            msg("output", "HEADLESS-1", "workspace", "1")
            msg("switcher")  # Switching back focused B.
            _, selected, titles, _ = opened(events.expect(opened, "switcher opened"))
            assert titles == ["B", "D", "A"] and selected == 1, (titles, selected)
            msg("switcher_cancel")
            events.expect(lambda l: "switcher-close" in l, "switcher cancelled")
            open_window("C")
            wait_for(lambda: windows()["C"]["output"] == "HEADLESS-2" and focused() == "C",
                     "C on HEADLESS-2 and focused")

            # Every window, most recently focused first, on the focused output.
            msg("switcher")
            where, selected, titles, entries = opened(events.expect(opened, "switcher opened"))
            assert where == "HEADLESS-2", where
            assert titles == ["C", "B", "D", "A"], titles
            assert selected == 1, selected
            d = entries[2]
            assert d[2] == "HEADLESS-1" and d[3] == "2" and d[4] == "0", d
            msg("switcher")
            events.expect(lambda l: l == ["switcher-select 2"], "next")
            msg("switcher_prev")
            msg("switcher_prev")
            msg("switcher_prev")
            events.expect(lambda l: l == ["switcher-select 1", "switcher-select 0",
                                          "switcher-select 3"], "previous, wrapping around")
            assert focused() == "C", "the switcher focused a window before confirming"

            # Confirming focuses A, on its output's current workspace.
            msg("switcher_confirm")
            events.expect(lambda l: l == ["switcher-close"], "switcher closed")
            wait_for(lambda: focused() == "A", "A focused")

            # D is on a workspace HEADLESS-1 does not show: picking it switches there.
            msg("switcher")
            _, _, titles, _ = opened(events.expect(opened, "switcher reopened"))
            assert titles == ["A", "C", "B", "D"], titles
            msg("switcher_confirm", "4")
            wait_for(lambda: focused() == "D" and windows()["D"]["visible"] and
                     not windows()["A"]["visible"], "D focused on workspace 2")
            events.expect(lambda l: "switcher-close" in l, "closed after picking")

            # Cancelling leaves focus alone.
            msg("switcher")
            events.expect(opened, "switcher opened to cancel")
            msg("switcher_cancel")
            events.expect(lambda l: l == ["switcher-close"], "cancelled")
            assert focused() == "D"

            # A listed window closing leaves the list; the selection stays on its window.
            msg("switcher")
            _, selected, titles, _ = opened(events.expect(opened, "opened before a close"))
            assert titles == ["D", "A", "C", "B"] and selected == 1, (titles, selected)
            msg("switcher")  # C selected
            events.expect(lambda l: l == ["switcher-select 2"], "C selected")
            clients["B"].terminate()
            clients["B"].wait(timeout=30)
            processes.remove(clients["B"])
            _, selected, titles, _ = opened(events.expect(opened, "list without B"))
            assert titles == ["D", "A", "C"] and selected == 2, (titles, selected)
            clients["C"].terminate()
            clients["C"].wait(timeout=30)
            processes.remove(clients["C"])
            _, selected, titles, _ = opened(events.expect(opened, "list without C"))
            assert titles == ["D", "A"] and selected == 1, (titles, selected)
            msg("switcher_confirm")
            wait_for(lambda: focused() == "A", "A focused after the list shrank")

            error = subprocess.run([compositor, "msg", "switcher_confirm", "x"], env=env,
                                   capture_output=True, text=True, timeout=30)
            assert error.returncode != 0 and "switcher_confirm takes" in error.stdout + \
                error.stderr, (error.stdout, error.stderr)

            # The keyboard: Alt+Tab twice then releasing Alt; Alt+Shift+Tab; Escape.
            wtype = shutil.which("wtype")
            if wtype:
                def type_keys(*arguments):
                    subprocess.run([wtype, *arguments], env=env, check=True, timeout=30)

                events.lines = []
                type_keys("-M", "alt", "-k", "Tab", "-m", "alt")
                wait_for(lambda: focused() == "D", "Alt+Tab picks the previous window, D")
                events.expect(lambda l: "switcher-close" in l, "closed on releasing Alt")
                type_keys("-M", "alt", "-M", "shift", "-k", "Tab", "-m", "shift", "-m", "alt")
                wait_for(lambda: focused() == "A", "Alt+Shift+Tab picks the last, A")
                type_keys("-M", "alt", "-k", "Tab", "-k", "Escape", "-m", "alt")
                events.expect(lambda l: "switcher-close" in l, "Escape closes")
                time.sleep(0.2)
                assert focused() == "A", "Escape still switched"
                print("Keyboard switching passed")
            else:
                print("wtype not found: keyboard switching skipped")
            print("Window switcher passed")
        finally:
            for process in reversed(processes):
                process.terminate()
                process.wait(timeout=30)
