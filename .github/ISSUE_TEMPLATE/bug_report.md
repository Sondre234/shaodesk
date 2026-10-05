---
name: Bug report
about: Something in shaodesk does not work as it should
labels: bug
---

**What happened, and what you expected instead**


**How to make it happen**

1.
2.

**System**

- `shaodesk --version` (both lines):
- GPU and driver (for example "NVIDIA RTX 4090, proprietary 580.xx" or "AMD RX 7800, Mesa 25.2"):
- wlroots package version, if it differs from the one `--version` names:
- Distribution:
- How shaodesk ran: nested inside (which desktop?), `--session` from a text console, or from a
  display manager (which one?):
- Monitors, if the problem involves them (how many, resolutions and scales):

**Configuration**

Your `~/.config/shaodesk/init.lua` (and `theme.lua`, if it has one), or "the default". Leave out
anything private.

```lua

```

**Log**

shaodesk, the shell and the programs they start log to standard error. From a text console, start
it with `shaodesk --session 2> ~/shaodesk.log` (nested: `shaodesk 2> ~/shaodesk.log`) and make the
problem happen. A display manager keeps the output in its own log, such as SDDM's
`~/.local/share/sddm/wayland-session.log`, or the journal under systemd (`journalctl --user -b`).
Attach the file, or paste the part around the problem:

```

```
