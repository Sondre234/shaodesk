# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows that ask for attention (xdg-activation) while unfocused: windows.activation decides
between marking them urgent, focusing them and ignoring the request; `get urgent`, the
subscription and focus_urgent report and use the marks; focusing clears them; and (with grim) the
border pulses and holds the urgent color."""
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""


def settings(activation, border=0):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = false }},
    windows = {{ activation = "{activation}", border_width = {border}, urgent_color = "#ff9e64",
                 border_color = "#7da8ff", border_inactive_color = "#404a5c" }},
    bindings = {{ {{ mods = {{ "Alt" }}, key = "u", action = "focus_urgent" }} }},
}}"""


with tempfile.TemporaryDirectory(prefix="shaodesk-urgent-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(settings("urgent"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODESK_SOCKET"):
        env.pop(name, None)

    def msg(*words, ok=True):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def rows(request):
        return [line.split("\t") for line in msg("get", request).splitlines()]

    def urgent():
        """(app_id, workspace, focused) of each urgent window, longest waiting first."""
        return [(r[8], int(r[0]), r[1] == "1") for r in rows("urgent")]

    def focused():
        return [r[8] for r in rows("windows") if r[1] == "1"]

    def wait_for(predicate, message):
        harness.wait_for(predicate, processes, message,
                         detail=lambda: f"urgent {urgent()} focused {focused()}")

    def start(app_id):
        env_app = dict(env, SHAODESK_PROBE_APP_ID=app_id, SHAODESK_PROBE_TITLE=f"Window {app_id}")
        client = subprocess.Popen([probe, "--commands"], env=env_app, stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, text=True)
        processes.append(client)
        wait_for(lambda: focused() == [app_id], f"{app_id} mapped and focused")
        return client

    def ask(client):
        client.stdin.write("activate\n")
        client.stdin.flush()

    class Subscriber:
        """The control socket's state stream: the last complete state received."""

        def __init__(self):
            self.socket = socket.socket(socket.AF_UNIX)
            self.socket.connect(env["SHAODESK_SOCKET"])
            self.socket.sendall(b"subscribe\n")
            self.socket.settimeout(0.05)
            self.buffer = ""

        def state(self):
            try:
                while data := self.socket.recv(8192):
                    self.buffer += data.decode()
            except socket.timeout:
                pass
            blocks = self.buffer.split("tiling ")
            return "tiling " + blocks[-1] if len(blocks) > 1 else ""

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            harness.wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes,
                             "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
            assert "xdg_activation_v1" in subprocess.run(
                [probe, "--globals"], env=env, capture_output=True, text=True).stdout
            assert urgent() == []
            subscriber = Subscriber()
            harness.wait_for(lambda: "urgent 0" in subscriber.state(), processes, "first state")

            # By default a request marks the window and leaves focus where it is.
            a = start("urgent-a")
            b = start("urgent-b")
            ask(a)
            wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a marked urgent")
            assert focused() == ["urgent-b"]
            harness.wait_for(lambda: "urgent 1\n" in subscriber.state(), processes,
                             "the subscription reports the count")
            # The window switcher marks it too.
            msg("switcher")
            harness.wait_for(lambda: subscriber.state() is not None and "switcher-window" in subscriber.buffer,
                             processes,
                             "the switcher's list")
            listed = {f[0]: f for f in (l[len("switcher-window "):].split("\t")
                                        for l in subscriber.buffer.splitlines()
                                        if l.startswith("switcher-window "))}
            assert listed["urgent-a"][5] == "1" and listed["urgent-b"][5] == "0", listed
            msg("switcher_cancel")
            state = subscriber.state()
            assert "urgent-output " in state and re.search(r"urgent-output \S+ 1\n", state), state
            assert re.search(r"urgent-window \S+\t1\turgent-a\tWindow urgent-a\n", state), state

            # Asking again changes nothing; a focused window is never urgent.
            ask(a)
            ask(b)
            time.sleep(.3)
            assert urgent() == [("urgent-a", 1, False)]

            # The window that waited longest is first, and focus_urgent takes them in order.
            c = start("urgent-c")
            ask(b)
            wait_for(lambda: [u[0] for u in urgent()] == ["urgent-a", "urgent-b"], "a then b")
            msg("focus_urgent")
            assert focused() == ["urgent-a"] and [u[0] for u in urgent()] == ["urgent-b"]
            harness.wait_for(lambda: "urgent 1\n" in subscriber.state() and
                             "urgent-window" in subscriber.state() and
                             "urgent-a" not in subscriber.state().split("urgent-window")[-1],
                             processes, "focusing clears a's mark in the stream")
            msg("focus_urgent")
            assert focused() == ["urgent-b"] and urgent() == []
            msg("focus_urgent")  # nothing urgent: nothing happens
            assert focused() == ["urgent-b"]
            harness.wait_for(lambda: "urgent 0\n" in subscriber.state() and
                             "urgent-window" not in subscriber.state(), processes,
                             "all clear in the stream")

            # focus_urgent switches workspace, and the indicator's data names the workspace.
            msg("workspace", "2")
            d = start("urgent-d")
            ask(a)
            wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a urgent on workspace 1")
            assert int(msg("get", "workspace")) == 2 and focused() == ["urgent-d"]
            harness.wait_for(lambda: re.search(r"urgent-output \S+ 1\n", subscriber.state()),
                             processes, "workspace 1 is urgent while 2 is shown")
            msg("focus_urgent")
            assert int(msg("get", "workspace")) == 1 and focused() == ["urgent-a"]
            assert urgent() == []

            # Focusing a window any other way clears it too.
            msg("workspace", "2")
            ask(a)
            wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a urgent again")
            subprocess.run([probe, "--activate", "urgent-a"], env=env, check=True, timeout=5,
                           stdout=subprocess.DEVNULL)
            wait_for(lambda: urgent() == [] and focused() == ["urgent-a"],
                     "activation from the taskbar clears the mark")

            # A window that closes while urgent leaves nothing behind.
            msg("workspace", "2")
            ask(a)
            wait_for(lambda: len(urgent()) == 1, "a urgent a third time")
            subprocess.run([probe, "--close", "urgent-a"], env=env, check=True, timeout=5,
                           stdout=subprocess.DEVNULL)
            wait_for(lambda: urgent() == [], "closing clears the mark")
            assert a.wait(timeout=5) == 0
            processes.remove(a)
            harness.wait_for(lambda: "urgent 0\n" in subscriber.state(), processes,
                             "the stream is clear")

            # focus: the request focuses the window and switches workspace; ignore: nothing.
            config.write_text(settings("focus"))
            msg("reload")
            wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
            msg("workspace", "1")
            focus_before = focused()
            ask(c)
            wait_for(lambda: focused() == ["urgent-c"], "focus policy focuses the window")
            assert urgent() == [] and focus_before != ["urgent-c"]
            # ... also from another workspace.
            msg("workspace", "4")
            e = start("urgent-e")
            assert int(msg("get", "workspace")) == 4
            ask(c)
            wait_for(lambda: focused() == ["urgent-c"], "focus policy switches to the workspace")
            assert int(msg("get", "workspace")) != 4 and urgent() == []
            config.write_text(settings("ignore"))
            msg("reload")
            wait_for(lambda: log.read_text().count("Configuration reloaded") == 2, "second reload")
            ask(b)
            time.sleep(.4)
            assert urgent() == [] and focused() == ["urgent-c"]
            config.write_text(settings("urgent"))
            msg("reload")
            wait_for(lambda: log.read_text().count("Configuration reloaded") == 3, "third reload")
            ask(b)
            wait_for(lambda: [u[0] for u in urgent()] == ["urgent-b"], "urgent again after reload")
            msg("focus_urgent")

            # A window hidden in the scratchpad that asks for attention comes back with
            # focus_urgent, on the workspace being shown.
            wait_for(lambda: focused() == ["urgent-b"], "b focused")
            msg("move_to_scratchpad")
            wait_for(lambda: [r[2] for r in rows("windows") if r[8] == "urgent-b"] == ["1"],
                     "b hidden in the scratchpad")
            msg("workspace", "3")
            ask(b)
            wait_for(lambda: [u[0] for u in urgent()] == ["urgent-b"], "hidden b is urgent")
            msg("focus_urgent")
            wait_for(lambda: focused() == ["urgent-b"] and urgent() == [], "b shown by focus_urgent")
            assert [r[2] for r in rows("windows") if r[8] == "urgent-b"] == ["0"]

            for client in (b, c, d, e):
                client.stdin.close()
            for process in processes[1:]:
                process.terminate()
                process.wait(timeout=5)
            server.terminate()
            assert server.wait(timeout=5) == 0, log.read_text()
            print("Activation requests mark, focus, or are ignored by policy; focus_urgent works")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
