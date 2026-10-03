# SPDX-License-Identifier: GPL-3.0-or-later
"""Hammer the control socket with every action, odd arguments, and raw garbage while a window
is mapped: the compositor must stay alive, answer afterwards, and shut down cleanly."""
import os
from pathlib import Path
import random
import re
import signal
import socket
import subprocess
import sys
import tempfile

from harness import wait_for

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])
ACTIONS = """close cycle snap_left snap_right maximize restore tile reload fullscreen workspace
move_to_workspace workspace_next workspace_prev workspace_back toggle_tiling layout_next
layout_prev layout_dwindle layout_master layout_spiral layout_monocle layout_scroll
scroll_left scroll_right column_widen column_narrow column_cycle_width consume_left consume_right
expel center_column promote focus_next
focus_prev swap_next swap_prev master_grow master_shrink master_more master_less toggle_floating
launcher focus_left focus_right focus_up focus_down move_left move_right move_up move_down
move_to_scratchpad scratchpad_show toggle_sticky resize_left resize_right resize_up resize_down
switcher switcher_prev switcher_confirm switcher_cancel get output subscribe""".split()
ARGUMENTS = ["", "0", "1", "-1", "4", "5", "99999999999999999999", "-99999999999999999999", "abc",
             "1 2", "1\t2", "0x10", "1e3", "%s%n", "'", "\"", "HEADLESS-1", "nonexistent",
             "workspaces", "windows", "outputs", "tiling", "animations", "x" * 400]

with tempfile.TemporaryDirectory(prefix="shaode-fuzz-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(Path(example).read_text().replace("xwayland = true", "xwayland = false"))
    log = root / "compositor.log"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "SHAODE_SOCKET"):
        env.pop(name, None)

    def talk(data, read=True):
        """Sends raw bytes on a fresh connection and returns the reply."""
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(5)
            sock.connect(path)
            reply = b""
            try:
                sock.sendall(data)
                if not read:
                    return b""
                sock.shutdown(socket.SHUT_WR)
                while chunk := sock.recv(4096):
                    reply += chunk
            except (ConnectionResetError, BrokenPipeError):
                pass  # the compositor hung up on a request it will not answer
            return reply

    with log.open("w") as output:
        server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                  env=env, stdout=output, stderr=output)
        processes = [server]
        try:
            wait_for(lambda: "Running Wayland compositor" in log.read_text(), processes, "startup")
            text = log.read_text()
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
            path = re.search(r"Control socket: (\S+)", text)[1]
            client = subprocess.Popen([probe, "--external-control"], env=env,
                                      stdout=subprocess.DEVNULL)
            processes.append(client)
            wait_for(lambda: talk(b"get windows\n").count(b"\n") == 2, processes, "window mapped")

            rng = random.Random(int(os.environ.get("SHAODE_FUZZ_SEED", 1234)))
            for action in ACTIONS:
                for argument in ARGUMENTS:
                    if action == "subscribe":
                        continue
                    reply = talk(f"{action} {argument}\n".encode())
                    assert reply.startswith((b"ok", b"error")), (action, argument, reply)
                    assert server.poll() is None, (action, argument)
                    if action in ("output", "get") and argument in ("", "0"):
                        continue
                    talk(f"output {argument} {action}\n".encode())
            assert server.poll() is None

            # Raw garbage: binary, NULs, unterminated and oversized lines, early disconnects.
            for _ in range(200):
                blob = bytes(rng.randrange(256) for _ in range(rng.choice([1, 8, 100, 511, 512, 513, 4000])))
                talk(blob, read=rng.random() < .5)
                talk(blob.replace(b"\n", b"") + b"\n", read=rng.random() < .5)
            talk(b"\0\n")
            talk(b"workspace 2\0garbage\n")
            talk(b"x" * 100000, read=False)
            talk(b"", read=False)
            talk(b"get windows\n", read=False)  # hang up without reading the reply
            # Subscribers that vanish or never read must not stall or crash the compositor.
            crowd = []
            for _ in range(100):  # a burst well past a small listen backlog
                sock = socket.socket(socket.AF_UNIX)
                sock.connect(path)
                sock.sendall(b"subscribe\n")
                crowd.append(sock)
            for index, sock in enumerate(crowd):
                if index % 2:
                    sock.close()
            for _ in range(20):
                talk(b"workspace_next\n")
            for sock in crowd[::2]:
                sock.close()

            talk(b"workspace 1\n")
            assert server.poll() is None
            assert talk(b"get windows\n").startswith(b"ok\n")  # "close" may have ended the window
            assert talk(b"get workspace\n") == b"ok\n1\n"
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=30) == 0, log.read_text()
            print("Control socket survived every action, odd argument, and garbage input")
        except Exception:
            print(log.read_text(), file=sys.stderr)
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
