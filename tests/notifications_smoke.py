# SPDX-License-Identifier: GPL-3.0-or-later
"""The notification daemon and the on-screen display in the shell, end to end: a private session
bus (a dbus-daemon this test starts and kills) and a headless compositor with the shell. Cards
appear top right, a click runs the default action, hovering holds the timer, do-not-disturb keeps
them away, `shaodesk msg osd` draws the pill, and the bell's history opens. The shell never sees
the real session bus."""
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import harness
from harness import wait_for as _wait_for

compositor, shell, notify, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = shutil.which("grim")
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the notification daemon was not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" }, ["HEADLESS-2"] = { mode = "1280x720" } } },
    notifications = { timeout = 60000 },
    osd = { timeout = 700 },
}"""
SCREEN = (2560, 720)  # two outputs side by side; HEADLESS-2 is the one at the origin
PANEL = (21, 30, 44)  # shell.panel_color

with tempfile.TemporaryDirectory(prefix="shaodesk-notify-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    (root / "bus.conf").write_text(
        f'<busconfig><type>session</type><listen>unix:dir={directory}</listen>'
        '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/>'
        '<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
    compositor_log, shell_log = root / "compositor.log", root / "shell.log"
    # A backlight of its own, which the shell polls in place of /sys.
    backlight = root / "sys" / "class" / "backlight" / "fake"
    backlight.mkdir(parents=True)
    (backlight / "max_brightness").write_text("100\n")
    (backlight / "brightness").write_text("50\n")
    processes = []
    bus = subprocess.Popen(["dbus-daemon", f"--config-file={root / 'bus.conf'}", "--nofork",
                            "--print-address=1"], stdout=subprocess.PIPE, text=True)
    processes.append(bus)
    address = bus.stdout.readline().strip()
    assert address.startswith("unix:"), address
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=directory, XDG_DATA_DIRS=directory, XDG_STATE_HOME=directory,
               DBUS_SESSION_BUS_ADDRESS=address, WLR_HEADLESS_OUTPUTS="2",
               SHAODESK_SYSFS=str(root / "sys"))
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)

    def wait_for(predicate, message, timeout=6):
        _wait_for(predicate, processes, message, timeout=timeout,
                  detail=lambda: shell_log.read_text()[-800:])

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def log():
        return shell_log.read_text()

    def count(text):
        return log().count(text)

    def send(*words):
        """Notifies; returns the id."""
        result = subprocess.run([notify, "notify", *words], env=env, capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        return int(result.stdout)

    def shot():
        if not grim:
            return None
        return harness.grab(grim, env, "HEADLESS-2")

    def close_to(color, at, message, tolerance=8):
        picture = shot()
        if picture is None:
            return
        pixel = picture.at(*at)
        assert all(abs(a - b) <= tolerance for a, b in zip(pixel, color)), (message, pixel, at)

    with compositor_log.open("w") as out, shell_log.open("w") as shell_out:
        try:
            server = subprocess.Popen([compositor, "--headless", "--config", str(config)],
                                      env=env, stdout=out, stderr=out)
            processes.append(server)
            wait_for(lambda: "Running Wayland compositor" in compositor_log.read_text(),
                     "compositor startup")
            env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)",
                                               compositor_log.read_text())[1]
            env["SHAODESK_SOCKET"] = re.search(r"Control socket: (\S+)",
                                             compositor_log.read_text())[1]
            desktop = subprocess.Popen([shell, "--config", str(config)], env=env,
                                       stdout=shell_out, stderr=shell_out)
            processes.append(desktop)
            wait_for(lambda: "serving org.freedesktop.Notifications" in log(), "the daemon serving")
            info = subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                                  timeout=10)
            assert info.stdout.strip() == "shaodesk 1.2", info

            watcher = subprocess.Popen([notify, "watch"], env=env, stdout=subprocess.PIPE)
            processes.append(watcher)
            events = []
            partial = b""

            def pump(timeout):
                """Reads what the watcher printed into `events`, a line each. Reads the pipe
                itself: a buffered reader would hold lines select() no longer reports."""
                global partial
                ready, _, _ = select.select([watcher.stdout], [], [], timeout)
                if ready:
                    partial += os.read(watcher.stdout.fileno(), 4096)
                    *lines, partial = partial.split(b"\n")
                    events.extend(line.decode().strip() for line in lines)

            while "watching" not in events:
                pump(2)
            events.remove("watching")

            def event(kind, number, extra=None, timeout=6):
                """Waits for a signal line such as "closed 3 2" from the watcher."""
                want = f"{kind} {number}" + (f" {extra}" if extra is not None else "")
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    if want in events:
                        events.remove(want)
                        return
                    pump(0.1)
                raise AssertionError(f"no '{want}'; saw {events}\n{log()[-600:]}")

            virtual = subprocess.Popen([pointer_probe, str(SCREEN[0]), str(SCREEN[1])], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(virtual)
            assert virtual.stdout.readline().strip() == "ready"

            def pointer(*words):
                virtual.stdin.write(" ".join(words) + "\n")
                virtual.stdin.flush()
                assert virtual.stdout.readline().strip() == "done"

            def layers():
                return msg("get", "layers")

            # A card appears in the top right corner of the output, on its own layer surface.
            pointer("move", "300", "300")
            first = send("Hi", "--timeout", "0", "--action", "default=Open")
            wait_for(lambda: count("notifications shown on HEADLESS-2") == 1, "the card shown")
            wait_for(lambda: "shaodesk-notifications" in layers(), "the card surface mapped")
            time.sleep(0.5)  # the slide in
            close_to(PANEL, (1200, 60), "a card top right")
            close_to((35, 46, 64), (300, 200), "background elsewhere", tolerance=30)

            # Clicking it runs its default action and dismisses it.
            pointer("move", "1150", "60")
            time.sleep(0.3)  # the surface takes the pointer's entry before a press
            pointer("click", "left")
            event("action", first, "default")
            event("closed", first, 2)
            wait_for(lambda: count("notifications hidden on HEADLESS-2") == 1, "the surface gone")

            # The application can close its own notification.
            second = send("Second", "--timeout", "0")
            wait_for(lambda: count("notifications shown") == 2, "second card shown")
            assert subprocess.run([notify, "close", str(second)], env=env, timeout=10).returncode == 0
            event("closed", second, 3)
            wait_for(lambda: count("notifications hidden") == 2, "surface gone after CloseNotification")

            # A timeout expires it; the surface goes once the card has slid away.
            third = send("Brief", "--timeout", "300")
            event("closed", third, 1)
            wait_for(lambda: count("notifications hidden") == 3, "surface gone after expiry")

            # Hovering a card holds its timer: it outlives its timeout, then goes once the pointer
            # leaves.
            fourth = send("Held", "--timeout", "800")
            wait_for(lambda: count("notifications shown") == 4, "fourth card shown")
            time.sleep(0.35)
            pointer("move", "1150", "60")
            time.sleep(1.6)
            assert not any(e.startswith(f"closed {fourth}") for e in events), events
            pointer("move", "300", "400")
            event("closed", fourth, 1)
            wait_for(lambda: count("notifications hidden") == 4, "surface gone after hover")

            # Replacing keeps the id; the card is updated, not doubled.
            fifth = send("Version one", "--timeout", "0")
            assert send("Version two", "--timeout", "0", "--replaces", str(fifth)) == fifth
            time.sleep(0.4)
            unrelated = send("Unrelated", "--timeout", "0")
            assert unrelated != fifth
            for number in (fifth, unrelated):
                subprocess.run([notify, "close", str(number)], env=env, check=True, timeout=10)
                event("closed", number, 3)
            wait_for(lambda: count("notifications hidden") == 5, "surface gone")

            # A real libnotify client (notify-send, where it is installed) gets its action back.
            libnotify = shutil.which("notify-send")
            if libnotify:
                shown = count("notifications shown")
                client = subprocess.Popen(
                    [libnotify, "-a", "libnotify", "-w", "-t", "0", "-A", "default=Open",
                     "-h", "int:value:70", "Real client", "with <b>markup</b>"], env=env,
                    stdout=subprocess.PIPE, text=True)
                processes.append(client)
                wait_for(lambda: count("notifications shown") == shown + 1, "notify-send's card")
                time.sleep(0.5)
                pointer("move", "1150", "60")
                time.sleep(0.3)
                pointer("click", "left")
                assert client.stdout.readline().strip() == "default"
                assert client.wait(timeout=10) == 0
                processes.remove(client)
                wait_for(lambda: count("notifications hidden") == shown + 1, "its card gone")

            # Do not disturb: the bell keeps them, no card opens; critical ones still do.
            shown_before = count("notifications shown")
            osd_before = count("osd shown")
            # The shell subscribes asynchronously: resend until it has heard.
            for _ in range(20):
                msg("dnd", "on")
                try:
                    wait_for(lambda: count("osd shown") > osd_before, "the do-not-disturb display",
                             timeout=0.5)
                    break
                except harness.Timeout:
                    pass
            assert count("osd shown") == osd_before + 1
            quiet = send("Quiet", "--timeout", "0")
            time.sleep(0.8)
            assert count("notifications shown") == shown_before
            loud = send("Fire", "--urgency", "2")
            wait_for(lambda: count("notifications shown") == shown_before + 1,
                     "a critical card shown in do-not-disturb")
            subprocess.run([notify, "close", str(loud)], env=env, check=True, timeout=10)
            event("closed", loud, 3)
            wait_for(lambda: count("osd hidden") >= 1 and count("osd hidden") == count("osd shown"),
                     "the first display gone")
            msg("dnd", "off")
            wait_for(lambda: count("osd shown") == osd_before + 2, "the display for turning it off")
            assert quiet > 0
            wait_for(lambda: count("osd hidden") == count("osd shown"), "the display gone")
            msg("dnd", "toggle")
            msg("dnd", "toggle")
            result = subprocess.run([compositor, "msg", "dnd", "sideways"], env=env,
                                    capture_output=True, text=True, timeout=5)
            assert "usage" in result.stdout + result.stderr, result

            # The on-screen display: a pill at the bottom centre that fades away.
            time.sleep(1.2)
            assert "osd hidden" in log()
            hidden = count("osd hidden")
            shown = count("osd shown")
            msg("osd", "Volume", "40")
            wait_for(lambda: count("osd shown") == shown + 1, "the display shown")
            time.sleep(0.3)
            # Above the label and the level: how far the label reaches depends on the fonts.
            close_to(PANEL, (600, 612), "the display's pill")
            wait_for(lambda: count("osd hidden") == hidden + 1, "the display faded out", timeout=4)
            bare = subprocess.run([compositor, "msg", "osd"], env=env, capture_output=True,
                                  text=True, timeout=5)
            assert "usage" in bare.stdout + bare.stderr, bare

            # A backlight change shows the display too (the level is read from a fake sysfs).
            hidden = count("osd hidden")
            shown = count("osd shown")
            (backlight / "brightness").write_text("20\n")
            wait_for(lambda: count("osd shown") == shown + 1, "the display for a brightness change")
            time.sleep(0.3)
            close_to(PANEL, (600, 612), "the brightness display's pill")
            wait_for(lambda: count("osd hidden") == hidden + 1, "the brightness display faded out",
                     timeout=4)

            # The bell's history: opened by the notification_history action.
            before = shot()
            for _ in range(20):
                msg("notification_history")
                time.sleep(0.3)
                after = shot()
                if before is None or after.at(1100, 560) != before.at(1100, 560):
                    break
            close_to(PANEL, (1100, 560), "the history popover", tolerance=10)

            # Cards go to the monitor with the focus (a click on its desktop gives it), and stay
            # there while they last.
            pointer("move", "1900", "300")
            time.sleep(0.3)
            pointer("click", "left")
            time.sleep(0.4)
            other_shown = count("notifications shown on HEADLESS-1")
            moved = send("Elsewhere", "--timeout", "0")
            wait_for(lambda: count("notifications shown on HEADLESS-1") == other_shown + 1,
                     "a card on the other monitor")
            pointer("move", "300", "300")
            time.sleep(0.3)
            pointer("click", "left")
            time.sleep(0.4)
            stays = send("Still there", "--timeout", "0")
            time.sleep(0.5)
            assert count("notifications shown on HEADLESS-1") == other_shown + 1
            assert count("notifications shown on HEADLESS-2") == count("notifications hidden on HEADLESS-2")
            for number in (moved, stays):
                subprocess.run([notify, "close", str(number)], env=env, check=True, timeout=10)
                event("closed", number, 3)
            wait_for(lambda: count("notifications hidden on HEADLESS-1") == other_shown + 1,
                     "the other monitor's cards gone")
            # The display follows the focus too.
            pointer("move", "1900", "300")
            time.sleep(0.3)
            pointer("click", "left")
            time.sleep(0.4)
            elsewhere = count("osd shown on HEADLESS-1")
            msg("osd", "Elsewhere")
            wait_for(lambda: count("osd shown on HEADLESS-1") == elsewhere + 1,
                     "the display on the other monitor")

            # The setting is followed on reload: off releases the name, on takes it again.
            config.write_text(CONFIG.replace("notifications = { timeout = 60000 }",
                                             "notifications = { enabled = false }"))
            desktop.send_signal(signal.SIGHUP)

            def serving():
                return subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                                      timeout=10).returncode == 0

            wait_for(lambda: not serving(), "the name released by notifications.enabled = false")
            config.write_text(CONFIG)
            desktop.send_signal(signal.SIGHUP)
            wait_for(serving, "the name taken again")
            assert log().count("serving org.freedesktop.Notifications") == 2, log()

            # Quitting frees the name for another daemon.
            desktop.terminate()
            desktop.wait(timeout=10)
            processes.remove(desktop)
            gone = subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                                  timeout=10)
            assert gone.returncode != 0, gone.stdout
            for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
                assert message not in log(), log()
            print("notification cards, actions, expiry, hover, do-not-disturb, osd and history passed")
        except Exception:
            print(compositor_log.read_text()[-1500:], log()[-3000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
