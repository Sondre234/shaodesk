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

shaodesk, the shell and the programs they start log to standard error. A session started with
`shaodesk-session`, as the display-manager entry does, keeps it in
`~/.local/state/shaodesk/session.log` (`session.log.old` is the one before). Nested, start it with
`shaodesk 2> ~/shaodesk.log` and make the problem happen. Attach the file, or paste the part
around the problem:

```

```
