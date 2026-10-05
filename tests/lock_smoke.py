# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise ext-session-lock-v1 on a private headless compositor."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, lock_probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    # A crashed locker leaves the session locked; a new locker may take over.
    for mode in ("abandon", "check-locked", "cycle"):
        subprocess.run([lock_probe, mode], env=desktop.env, check=True, timeout=20)
    text = desktop.log.read_text()
    assert "Lock client vanished" in text and "Session unlocked" in text, text
print("Session lock, rejection, focus isolation, abandonment, and unlock passed")
