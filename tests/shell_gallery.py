# SPDX-License-Identifier: GPL-3.0-or-later
"""Every popup of tools/shell_gallery.py renders in both of its themes without a QML warning,
and each picture with a popup is taller than the bar alone, the surface having grown for it."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

tool, build, renderer = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location("shell_gallery", tool)
gallery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gallery)
popups = sys.argv[4:] or gallery.POPUPS

with tempfile.TemporaryDirectory(prefix="shaodesk-gallery-") as directory:
    command = [sys.executable, tool, build, directory, "--renderer", renderer]
    for popup in popups:
        command += ["--popup", popup]
    result = subprocess.run(command, capture_output=True, text=True, timeout=240)
    assert result.returncode == 0, result.stdout + result.stderr
    suffix = "-gpu" if renderer == "gpu" else ""
    sizes = {}
    for theme in gallery.THEMES:
        for popup in popups:
            picture = Path(directory) / f"{theme}-{popup}{suffix}.png"
            data = picture.read_bytes()
            assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{picture.name} is no PNG"
            sizes[theme, popup] = struct.unpack(">II", data[16:24])
    for (theme, popup), (width, height) in sizes.items():
        assert width >= 640, f"{theme}-{popup} is {width} pixels wide"
        if popup != "bar" and (theme, "bar") in sizes:
            assert height > sizes[theme, "bar"][1], f"{theme}-{popup} did not grow the surface"
print(f"{len(sizes)} popups rendered ({renderer})")
