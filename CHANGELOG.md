# Changelog

Notable changes, newest first. Dates are when the work landed. The project has not made a
tagged release yet; everything below is on `main`.

## Unreleased (2026-10-05)

- Power controls. New actions `lock`, `suspend`, `hibernate`, `reboot`, `poweroff`, `logout`
  and `power_menu`, bound to Super + Shift + L (lock) and Super + Escape (the power menu). The
  panel has a power button (`shell.widgets.power`) and the command palette the same entries,
  listing only what logind (systemd-logind or elogind) allows; restart, power off and log out
  ask first with a dialog that counts down `power.countdown` seconds. `lock` starts
  `power.lock_command` (`swaylock -f` unless set), and with `power.lock_before_sleep` the screen
  locks before any sleep, whether shaodesk, the lid or an idle daemon asks for it. Power off,
  reboot and log out first ask every window to close and give up, with the reason on the panel,
  when one is still open after `power.close_timeout` (an application asking whether to save),
  unless `power.force` is set. `shaodesk msg get power` shows what may run. A headless
  compositor uses only the logind on the bus `SHAODESK_LOGIN1_BUS` names, which the tests point
  at a fake one, so they can never power off the machine running them.

## Unreleased (2026-10-03)

- `layout.tiling_per_workspace = true` makes tiling a setting of each workspace rather than
  each monitor: Super + S, the panel button and `toggle_tiling` then switch only the current
  workspace, and a window moved to another workspace tiles or floats as that one does.
- Fullscreen a program asks for itself, like a browser's fullscreen video, stays over the
  panels when it loses focus, so a video on one monitor no longer shows the taskbar over it
  while you work on another. Raising another window on the same monitor still lowers it
  below that window and the panels.
- The project is now called shaodesk everywhere, and nothing answers to the old `shaode`
  name: the executables are `shaodesk` and `shaodesk-shell` (`shaodesk msg`,
  `shaodesk import`), the configuration lives in `~/.config/shaodesk`, state in
  `~/.local/state/shaodesk`, and the environment variables and CMake options start with
  `SHAODESK_` (`SHAODESK_SOCKET`, `SHAODESK_DEFAULT_CONFIG`, `SHAODESK_BUILD_SHELL`, ...).
  The session sets `XDG_CURRENT_DESKTOP=shaodesk` and installs `shaodesk-portals.conf` and
  `shaodesk.desktop`. To carry a setup over, move `~/.config/shaode` and
  `~/.local/state/shaode` to the new names, update launch scripts and key bindings that run
  `shaode`, and configure a fresh build directory.

## Unreleased (2026-09-28)

- The configuration reloads when it is saved, as in Hyprland: the compositor watches the `.lua`
  files in its directory (`auto_reload = false` turns this off). A file with an error, on a save
  or at startup, no longer leaves the old settings running or stops the session from starting:
  the default configuration loads in its place, and a red bar across the top of every monitor
  shows the error with its file and line until a save fixes it.
- Fullscreen from a binding or the title bar no longer covers the panel: the window fills the
  monitor above it. Fullscreen a program asks for itself, like a YouTube video, still covers the
  whole monitor, panel included.
- Clicking the focused window's taskbar button minimizes it from a bar on another monitor too:
  pointing at or clicking a panel there no longer counts as the bare desktop and takes the focus
  away first, which made the click restore the window instead.
- Appearance profiles: `profiles` names sets of `appearance`, `windows` and `shell` settings laid
  over the configuration, and `profile` picks the one to start with. Switch from the panel's
  right-click menu (Appearance), the command palette, or `shaodesk msg profile NAME|next|prev`; the
  choice is saved in `$XDG_STATE_HOME/shaodesk/profile` and kept across restarts. The example
  configuration ships `default` and `light`. A profile button on the panel (`shell.widgets.profiles`)
  lists them to switch with one click.
- Quality sweep: `tools/check-all.sh` builds, runs the whole suite, then runs it again under
  AddressSanitizer, UndefinedBehaviorSanitizer and leak detection (`--repeat N` hunts flaky
  tests). Fixes it led to: a client's app ID could forge lines in the overview description sent to
  the shell (a newline in it ran as a request); the overview's timer was never removed at
  shutdown; the control socket refused bursts of clients (listen backlog 8); one output that
  could not be magnified (rotated) switched the magnifier off everywhere until a restart; the
  volume control's tooltip was empty; the palette's selection jumped when the saved sessions
  arrived late; the example configuration's commented scroll bindings collided with default
  keys. New tests fuzz groups, configuration reloads and the control socket with seeds, and
  check that every binding action is in the README and every documentation example is accepted.
  `qmllint` is clean over the shell's QML. The shell also shows window titles and app names as
  plain text (a title such as `<b>x</b>` was read as markup) and no longer loads an icon from a
  file path a window names as its app ID.
- Notifications: the shell serves `org.freedesktop.Notifications` and shows cards (actions,
  markup, icons and images, progress, urgency, replacement, timeouts that pause on hover) in a
  corner of the focused monitor; the panel's bell keeps a history with an unread badge, and
  do-not-disturb (`dnd_toggle`, `dnd_on`, `dnd_off`, `shaodesk msg dnd`) silences the cards. On-screen
  display for volume, brightness and `shaodesk msg osd TEXT [PERCENT]`. See the `notifications` and
  `osd` settings and `notification_history` (Super + N).
- `windows.placement`: new floating windows can open `"cascade"`d (the default, as before),
  `"center"`ed, or `"smart"`, in the free space where they cover the other windows least.
- Magnetic edges (`windows.magnet`): a floating window being dragged or resized sticks to the edges of the
  output, the free area and other windows within `distance` pixels, with a guide line; a
  modifier (`bypass`, Shift) turns it off for a drag.
- Performance: while windows glide or fade, a pointer hit test no longer moves every running
  animation to its resting place and back (two scene updates each); it does so only for the
  animations that could be under the pointer. Pointer motion during a burst of layout changes with
  40 windows went from about 340 us to well under 100 us per event.
- Urgent windows: with `windows.activation = "urgent"` (the default) an unfocused application that
  asks for attention through xdg-activation, `_NET_WM_STATE_DEMANDS_ATTENTION` or an X11 urgency
  hint is marked urgent instead of taking focus. Its border pulses in `windows.urgent_color`
  (also without a `border_width`), and `focus_urgent` (Super + U) jumps to the one that asked
  first. `"focus"` restores the old behaviour, `"ignore"` drops requests. `shaodesk msg get urgent`
  and the subscription report them. The shell marks urgent windows on the taskbar (also
  stacked buttons and their hover list), the workspace indicator, the overview, the window
  switcher and the command palette.
- Performance: a pointer motion event makes one scene hit test instead of up to four (window,
  controls, tabs, resize band); with 40 windows it takes about a third of the time. The
  benchmark has a `pointer` scenario (`--pointer-probe`) and `get stats` counts motion events.
- Performance: `get frame_times` reports the last 4096 frames' time in `output_frame` and the interval
  between frames; `tools/bench/bench.py` has a `smooth` scenario (animations on, windows
  redrawing, layout changes and workspace switches) that prints their p50/p95/p99.
- The default build type is `RelWithDebInfo` when `CMAKE_BUILD_TYPE` is unset (single-config
  generators), so a plain build and distribution packages are optimised.
- `move_workspace_to_output` (Super + Ctrl + comma / period) sends a workspace with its windows
  and layout state to the monitor on the left or right, by name, `next` or `prev`;
  `swap_workspaces` trades all workspaces of two monitors. Windows glide across.
- Sessions keep the columns of the scrolling layout: `session save` records each column's width
  and each tile's column and place in its stack, and `session restore` sets them again.
- Hotplug: windows of an unplugged monitor (fullscreen ones too) move to the nearest one (tiles
  rejoin its tiling, workspace numbers kept; a monitor turned off in the configuration hands its
  floating windows over the same way, onto the workspace the other one shows) and return when it is plugged back in (`outputs.return_windows`).
  Headless sessions can plug virtual outputs with `shaodesk msg headless_output add|remove`.
- `layout.outputs`: per-monitor `tile_layout`, `master_ratio` and `master_count` for the
  workspaces not set by hand, applied live on reload and when a monitor returns. A ratio or
  master count changed by an action no longer freezes the workspace's layout against later
  configuration changes. `shaodesk msg get layout OUTPUT [WORKSPACE]`.
- Window swallowing: with `windows.swallow.enabled` a window started from a terminal (found
  through process ancestry) takes its tile or floating place and hides it; closing it brings
  the terminal back. `swallow_toggle` does it by hand; `windows.swallow.terminals` and
  `exceptions` choose what swallows.
- Shell performance: the shell draws with Qt's software renderer by default (`shell.renderer`,
  `"gpu"` for the old behaviour), the QML is compiled ahead of time and one engine serves every
  view, popups are made on first use, the clock ticks once a minute, and battery and network
  changes come from kernel messages instead of a 5 s poll. Measurements in
  `docs/performance.md`; `tools/shell_perf.py` reproduces them.
- Overview (Exposé), Super + O / `toggle_overview`: live thumbnails of the monitor's windows in
  a grid with a workspace strip; type to filter, arrows or mouse to pick, middle click to
  close, drag a thumbnail onto the strip to move it. See the `overview` settings and
  `overview_confirm`, `overview_cancel`.
- `peek` (hold a key) and `peek_toggle` fade every window out to show the desktop; see the
  `peek` settings.
- Performance: `tools/bench/bench.py` benchmarks the compositor headless (`docs/performance.md`);
  window opacity rules are matched only when a window's inputs change, cutting the cost of a
  commit several times over.
- Command palette (Super + P, `palette`): fuzzy search over windows, applications, workspaces,
  compositor actions and saved sessions in the shell, with `>`, `@`, `#` and `%` filters. Sticky
  windows moved from Super + P to Super + Shift + P.
- Window groups: `group_toggle`, `group_next`, `group_prev`, `ungroup` and `group_merge_*` put
  several windows in one slot under a strip of clickable tabs; windows opened while a group has
  focus join it (`features.groups`, `features.group_join_new`).
- Magnifier: `zoom_in`, `zoom_out`, `zoom_reset` and `zoom.scroll_modifier` magnify the output
  around the pointer with an eased level.
- Hot corners: `hot_corners` runs an action or command when the pointer rests in a screen
  corner.
- Night light: `night_light` settings warm the screen on a schedule (fixed times or a location)
  with the actions `night_light_toggle`, `night_light_on`, `night_light_off`, `night_light_auto`.
- Sessions: `shaodesk msg session save|restore|list|delete NAME` keeps window placement,
  layouts and workspaces, and `restore NAME launch` starts applications that are missing.
- Named workspaces: `layout.workspace_names` labels them in the panel and lets bindings and
  `shaodesk msg workspace NAME` reach them by name.
- Scrolling tile layout (`layout_scroll`, `tile_layout = "scroll"`), after niri and PaperWM:
  columns on an endless strip, a view that follows focus (`layout.scroll.follow`), column
  width presets, stacked windows, and the actions `scroll_left`, `scroll_right`,
  `column_widen`, `column_narrow`, `column_cycle_width`, `consume_left`, `consume_right`,
  `expel` and `center_column`.
- `focus_last` (Super + `): flip between the two most recently focused windows, across
  workspaces.
- Animations: per-kind duration and easing curve (cubic-bezier, spring, overshoot, and named
  presets), a global `speed`, glides that keep their velocity when retargeted, and late
  frames that finish animations instead of stuttering; focus changes fade a window's
  opacity and border color, switching workspace slides windows out and in, and fullscreen
  toggles glide the window (`animations.fullscreen`).
- `windows.dim_inactive` dims windows without focus with an animated fade.
- Taskbar: an application's windows share one stacked button (`group_windows = false`
  turns it off), reorderable by dragging; the default cursor theme is Adwaita.
- Alt + Tab window switcher over every window on every monitor and workspace.
- Focus follows the pointer between monitors, including empty ones.
- Monitors can be changed at runtime by wlr-output-management clients (`wlr-randr`, `kanshi`).
- `keyboard.variant` and `keyboard.model` settings.
- Wallpapers that fail to load are retried and reloaded on configuration changes.

## 2026-09-27

- Sway-style features, each behind a `features` toggle: scratchpad, sticky windows,
  workspace back-and-forth, window rule actions, and keyboard resizing of tiles and
  floating windows.
- Pin installed applications to the taskbar; icons in pinned slots; drag to reorder.
- Volume control in the panel.

## 2026-09-26

- Automatic tiling is a per-monitor setting (Super + S).
- A configuration can `extends = "default"` and hold only its changes.
- Mouse bindings, optionally restricted to matching windows or the desktop.
- Super + arrows move focus; Super + Shift + arrows move windows.
- Virtual keyboard and pointer support, so wtype and wlrctl can drive a session.
- X11 fixes: windows that map before association, fixed-size and dialog windows float.
- Windows may keep their own frame; flat window-control buttons.

## 2026-09-25

- `shaodesk import` carries Hyprland, Waybar, wallbash, and pywal looks into `theme.lua`.
- Per-monitor mode, scale, position, rotation, disable, and adaptive sync settings.
- Per-output workspaces, shown on each panel.
- Window open/close animations and tile glide.
- Print-key screenshots (region, output, window) through grim and slurp.
- Panel placement, floating margins, radius, translucency, and font settings; gaps,
  borders, and per-app opacity; pointer and touchpad settings.
- Right-click menus on empty bar space; the machine stays awake while a session is shown.

## 2026-09-24

- Hyprland-style dwindle tiling with a panel toggle.
- XWayland on demand, with a wlroots patch in `packaging/patches`.
- Browser and Electron protocols (linux-dmabuf, primary selection, xdg-activation,
  cursor-shape) and screen capture for screenshots and screen sharing through portals.
- Qt Quick shell: taskbar, launcher, desktop shortcuts, wallpaper, workspaces, control
  socket (`shaodesk msg`), fullscreen, session lock, idle notification and inhibit.
- Standalone `--session` backend (DRM/libinput) that survives VT switches; first
  physical tests on an AMD laptop and an NVIDIA three-monitor desktop.
- Gentoo QEMU/KVM test VM scripts; GPL-3.0-or-later license.

## 2026-09-23

- Validated Lua configuration, snapping and grid placement, and the nested wlroots
  compositor with focus follows mouse, move/resize, and reload.
- Installable build with DESTDIR staging and an opt-in session entry.
