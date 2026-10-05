# SPDX-License-Identifier: GPL-3.0-or-later
"""The system tray end to end: a private session bus (a dbus-daemon this test starts and kills), a
headless compositor with two outputs and the shell, and tray_probe playing the applications. An
item's icon shows on every monitor's panel, clicks and the wheel reach the application, its menu
opens and an entry picked reaches it, an item registered by object path (as Ayatana's are) comes
after, items that go passive or quit leave, and shell.widgets.tray = false lets go of the bus
names. The shell never sees the real session bus."""
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

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = shutil.which("grim")
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the tray was not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" }, ["HEADLESS-2"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
SCREEN = (2560, 720)
PANEL_HEIGHT = 52
PANEL = (21, 30, 44)  # shell.panel_color
MAGENTA, GREEN, CYAN = (255, 0, 255), (0, 255, 0), (0, 255, 255)


class Lines:
    """A process's output, a line at a time, read from the pipe itself: a buffered reader would
    hold lines select() no longer reports."""

    def __init__(self, process):
        self.process, self.partial, self.lines = process, b"", []

    def pump(self, timeout):
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if ready:
            self.partial += os.read(self.process.stdout.fileno(), 4096)
            *lines, self.partial = self.partial.split(b"\n")
            self.lines.extend(line.decode().strip() for line in lines)

    def expect(self, pattern, timeout=6):
        """The first line matching `pattern` (a regular expression), taken from the list."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                match = re.fullmatch(pattern, line)
                if match:
                    self.lines.remove(line)
                    return match
            self.pump(0.05)
        raise AssertionError(f"no line like '{pattern}'; saw {self.lines}")


with tempfile.TemporaryDirectory(prefix="shaodesk-tray-test-") as directory:
    root = Path(directory)
    config = root / "init.lua"
    config.write_text(CONFIG)
    (root / "bus.conf").write_text(
        f'<busconfig><type>session</type><listen>unix:dir={directory}</listen>'
        '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/>'
        '<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
    compositor_log, shell_log = root / "compositor.log", root / "shell.log"
    processes = []
    bus = subprocess.Popen(["dbus-daemon", f"--config-file={root / 'bus.conf'}", "--nofork",
                            "--print-address=1"], stdout=subprocess.PIPE, text=True)
    processes.append(bus)
    address = bus.stdout.readline().strip()
    assert address.startswith("unix:"), address
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
               QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=directory, XDG_DATA_DIRS=directory, XDG_STATE_HOME=directory,
               DBUS_SESSION_BUS_ADDRESS=address, WLR_HEADLESS_OUTPUTS="2")
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)

    def wait_for(predicate, message, timeout=8):
        _wait_for(predicate, processes, message, timeout=timeout,
                  detail=lambda: shell_log.read_text()[-800:])

    def msg(*words):
        result = subprocess.run([compositor, "msg", *words], env=env, capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def log():
        return shell_log.read_text()

    def start_probe(*options):
        process = subprocess.Popen([probe, *options], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE)
        processes.append(process)
        lines = Lines(process)
        lines.expect(r"registered \S+ /\S+")
        return process, lines

    def command(process, lines, line):
        process.stdin.write((line + "\n").encode())
        process.stdin.flush()
        lines.expect("ok")

    def icons(output, color, tolerance=12):
        """The centres of the runs of `color` along the middle of `output`'s panel, in the
        layout's coordinates, left to right; [] without grim."""
        shot = harness.grab(grim, env, output)
        x0, y0 = origins[output]
        y = shot.height - PANEL_HEIGHT // 2
        found, start = [], None
        for x in range(shot.width + 1):
            pixel = shot.at(x, y) if x < shot.width else (-100, -100, -100)
            close = all(abs(a - b) <= tolerance for a, b in zip(pixel, color))
            if close and start is None:
                start = x
            elif not close and start is not None:
                if x - start >= 6:
                    found.append((x0 + (start + x - 1) // 2, y0 + y))
                start = None
        return found

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
            origins = {}

            def outputs():
                for line in msg("get", "outputs").splitlines():
                    name, _, x, y = line.split("\t")[:4]
                    origins[name] = (int(x), int(y))
                return sorted(origins) == ["HEADLESS-1", "HEADLESS-2"]

            wait_for(outputs, "both outputs")
            desktop = subprocess.Popen([shell, "--config", str(config)], env=env,
                                       stdout=shell_out, stderr=shell_out)
            processes.append(desktop)
            wait_for(lambda: "shaodesk tray: serving org.kde.StatusNotifierWatcher" in log(),
                     "the shell serving the watcher")
            wait_for(lambda: log().count("shaodesk surface rendered: shaodesk taskbar") >= 2,
                     "both panels drawn")

            # An item registered by name: its icon (the probe's pixels) on both panels.
            first, first_out = start_probe("--color", "#ff00ff", "--title", "First")
            if grim:
                wait_for(lambda: icons("HEADLESS-1", MAGENTA) and icons("HEADLESS-2", MAGENTA),
                         "the icon on both panels")
                (x, y), = icons("HEADLESS-2", MAGENTA)
            else:
                x, y = None, None
            # The probe answers each layout request; the menu was read when the item came.
            first_out.expect("layout 0")

            virtual = subprocess.Popen([pointer_probe, str(SCREEN[0]), str(SCREEN[1])], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            processes.append(virtual)
            assert virtual.stdout.readline().strip() == "ready"

            def pointer(*words):
                virtual.stdin.write(" ".join(words) + "\n")
                virtual.stdin.flush()
                assert virtual.stdout.readline().strip() == "done"

            def approach(x, y):
                """Moves the pointer onto x, y in a few steps from just below it. The compositor
                tells a surface where the pointer is only when it moves, not when the surface grows
                under it, as the panel's does for a menu."""
                for step in (12, 8, 4, 0):
                    pointer("move", str(x), str(y + step))
                    time.sleep(0.05)
                time.sleep(0.2)

            def panel_at(x, y):
                """Whether the panel's colour is drawn at x, y of HEADLESS-2, as a menu is."""
                return all(abs(a - b) <= 8 for a, b in zip(harness.grab(grim, env, "HEADLESS-2").at(x, y), PANEL))

            if grim:
                # Clicks reach the application, with the icon's place on the screen.
                approach(x, y)
                pointer("click", "left")
                match = first_out.expect(r"activate (-?\d+) (-?\d+)")
                assert abs(int(match[1]) - x) <= 3 and abs(int(match[2]) - y) <= 3, (match[0], x, y)
                pointer("click", "middle")
                first_out.expect(r"secondary -?\d+ -?\d+")
                pointer("scroll", "15")
                first_out.expect(r"scroll -?\d+ vertical")

                # A right click opens the menu above the icon; the application hears of it. Its
                # last entry ("Quit", id 6) is the row just above the menu's bottom edge, 8 pixels
                # over the bar.
                pointer("click", "right")
                first_out.expect("abouttoshow 0")
                first_out.expect("event 0 opened")
                bar_top = y - PANEL_HEIGHT // 2
                quit_row = bar_top - 8 - 6 - 34 // 2
                wait_for(lambda: panel_at(x, quit_row), "the menu drawn")
                approach(x, quit_row)
                pointer("click", "left")
                first_out.expect("event 6 clicked")
                first_out.expect("event 0 closed")

                # A new icon is drawn; passive, the item leaves the panels, and comes back.
                command(first, first_out, "color #00ff00")
                wait_for(lambda: icons("HEADLESS-2", GREEN) and not icons("HEADLESS-2", MAGENTA),
                         "the new icon")
                command(first, first_out, "status Passive")
                wait_for(lambda: not icons("HEADLESS-2", GREEN), "the passive item hidden")
                command(first, first_out, "status Active")
                wait_for(lambda: icons("HEADLESS-2", GREEN), "the item back")

            # An item registered by path, as Ayatana's are, without Activate: after the first,
            # and a left click opens its menu.
            second, second_out = start_probe("--path", "--no-activate", "--color", "#00ffff")
            second_out.expect("layout 0")
            if grim:
                wait_for(lambda: icons("HEADLESS-2", CYAN), "the second icon")
                (cx, cy), = icons("HEADLESS-2", CYAN)
                (gx, _), = icons("HEADLESS-2", GREEN)
                assert gx < cx, (gx, cx)
                approach(cx, cy)
                pointer("click", "left")
                second_out.expect("abouttoshow 0")
                wait_for(lambda: panel_at(cx, cy - PANEL_HEIGHT // 2 - 30), "the second item's menu drawn")
                approach(cx, cy)
                pointer("click", "right")  # a second press closes it
                second_out.expect("event 0 closed")

            # An application quitting takes its icon away.
            first.terminate()
            first.wait(timeout=10)
            processes.remove(first)
            if grim:
                wait_for(lambda: not icons("HEADLESS-2", GREEN), "the icon of a quit application gone")
                assert icons("HEADLESS-1", CYAN)

            # shell.widgets.tray = false lets go of the names (the item hears the watcher go and
            # come back, and registers again); on again, the shell serves the watcher once more.
            config.write_text(CONFIG.replace("notifications = { enabled = false },",
                                             "notifications = { enabled = false },\n"
                                             "    shell = { widgets = { tray = false } },"))
            desktop.send_signal(signal.SIGHUP)
            if grim:
                wait_for(lambda: not icons("HEADLESS-2", CYAN), "the tray hidden")
            config.write_text(CONFIG)
            desktop.send_signal(signal.SIGHUP)
            wait_for(lambda: log().count("shaodesk tray: serving org.kde.StatusNotifierWatcher") == 2,
                     "the watcher served again")
            second_out.expect(r"registered \S+ /\S+")
            if grim:
                wait_for(lambda: icons("HEADLESS-2", CYAN), "the item back after the reload")

            for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read",
                            "Unable to assign"):
                assert message not in log(), log()
            print("tray icons on both panels, clicks, wheel, menu, passive, Ayatana items, "
                  "quitting and shell.widgets.tray passed" + ("" if grim else " (no grim: no pixels)"))
        except Exception:
            print(compositor_log.read_text()[-1500:], log()[-3000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
