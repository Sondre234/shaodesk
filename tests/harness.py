# SPDX-License-Identifier: GPL-3.0-or-later
"""Helpers shared by the integration tests."""
import time


def wait_for(predicate, processes, message, timeout=15, detail=None):
    """Polls `predicate` until it holds, failing if a process exits or time runs out."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for process in processes:
            assert process.poll() is None, f"process exited ({process.returncode}): {message}"
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError(f"timed out: {message}" + (f"; {detail()}" if detail else ""))


class Shot:
    """A screenshot decoded from grim's PPM output."""

    def __init__(self, data):
        header, rest = data.split(b"\n255\n", 1)
        magic, size = header.split(b"\n", 1)
        assert magic == b"P6", magic
        self.width, self.height = (int(n) for n in size.split())
        self.pixels = rest
        assert len(rest) == 3 * self.width * self.height

    def at(self, x, y):
        i = 3 * (int(y) * self.width + int(x))
        return tuple(self.pixels[i:i + 3])


def grab(grim, env, output=None):
    """A Shot of the compositor's output, or None when grim is not installed (`grim` empty)."""
    import subprocess
    if not grim:
        return None
    command = [grim, "-t", "ppm"] + (["-o", output] if output else []) + ["-"]
    return Shot(subprocess.run(command, env=env, capture_output=True, check=True,
                               timeout=20).stdout)
