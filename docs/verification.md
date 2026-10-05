# Verification: checkpoint log

Dated records of what was checked, automatically and by hand, and what was not. New
checkpoints are appended; the automated part is the `ctest` suite, which CI also runs
headless in an Arch Linux container (see `.github/workflows/ci.yml`).

## First nested compositor checkpoint

Verified on 2026-09-23 with wlroots 0.20.2, GCC 16.2.1, Lua 5.4.8, and Wayland
1.26.0 on Arch Linux. Nested graphics ran inside Hyprland on an NVIDIA RTX 4090
using the wlroots GLES2 renderer. Nothing was installed into the login/session
configuration, and the existing desktop remained running.

## Automated checks

- CMake/Ninja build: passed without compiler warnings.
- Configuration tests: valid/computed settings, key/modifier matching, malformed
  values, unknown settings/actions, duplicate bindings, bounded Lua evaluation,
  and preserving configuration after a failed load.
- Placement tests: output bounds, non-overlap, gaps, negative output origins,
  small outputs, and invalid inputs.
- Headless compositor integration: four actual xdg-shell client connections,
  shared-memory rendering with frame callbacks, maximize/restore, unmap/destroy,
  valid SIGHUP reload, invalid reload rejection with continued rendering,
  invalid `--check-config` exit status, and SIGTERM exit status zero with socket
  removal. Uses Pixman and a private temporary `XDG_RUNTIME_DIR`.
- C/C++ formatting and `git diff --check`: passed.

Run all automated checks with `ctest --test-dir build --output-on-failure`.

## Nested runtime checks

This section records the checkpoint as it was run. Its keys were the then-default Alt
bindings (Alt + drag, Alt + F4, Alt + Shift + Escape); the default modifier is now Super
(see [config/init.lua](../config/init.lua)), and there are no default bindings for the
last two: close is Super + C and quit is Super + M.

Real Kitty clients were opened inside temporary nested compositor instances.
Input was sent to those windows through Hyprland's documented dispatch API;
window-only screenshots were inspected. Verified:

- Background and application rendering, keyboard text input, window cycling.
- Keyboard half-screen snapping, grid arrangement with two applications,
  maximize and restore.
- Alt + left-button movement and Alt + right-button resizing.
- Alt + F4 closes a client (verified by its process exiting with status zero).
- The host window can be resized; the background follows its dimensions.
- Closing the host window shuts down the compositor with status zero.
- Alt + Shift + Escape terminates the nested compositor.

An oversized initial Kitty window exposed a placement issue. Newly mapped
windows now receive a bounded size suggestion and a reachable initial position.
The updated executable was rebuilt and exercised in the nested runtime checks.
Client minimum-size requirements can still exceed very small outputs.

All temporary test compositor instances and terminals were closed after testing.
Screenshots and host-control helpers were kept outside the repository. The
repository's automated tests do not control the user's desktop.

## Remaining coverage and limitations

Physical-device testing, hotplug, multi-monitor behavior, fractional scaling,
clipboard interoperability, client-side title-bar grabs, and extended soak tests
remain. The compositor checkpoint did not include shell UI, workspaces, persistent tiling,
fullscreen handling, XWayland, portals, or session locking (all were added later;
see the checkpoints below and [features.md](features.md)). Interactive resize
positions are applied before clients submit their replacement buffers. This is
a development checkpoint for nested use, not a complete desktop session.

Host input testing followed the
[Hyprland dispatcher documentation](https://wiki.hypr.land/configuring/core/dispatchers/).

## Shell protocol checkpoint

The headless protocol probe now maps an actual layer-shell panel with a
48-pixel exclusive zone and verifies that maximization leaves that area free.
It also receives the window title through foreign-toplevel-management and
exercises minimize, restore/activate, close, and handle removal after unmapping.
The integration test repeats those operations across configuration reloads.

## Qt shell preview checkpoint

On 2026-09-24, the preview build compiles with Qt 6.11.2 and GIO 2.88.3. CTest
renders the taskbar/launcher and desktop QML into PNGs with the offscreen software
backend, using an isolated application directory. Both previews were also
visually inspected. Configuration tests cover shell colors, height, wallpaper,
launchers, and malformed values. The compositor-only build remains supported.

The shell's actual Qt task model also connects to the headless compositor and
controls a real xdg-shell client. Its test verifies title/app ID, active state,
click-to-minimize, restore/activate, maximize/restore, show desktop, close, and
row removal. Stale task IDs after closing a window are harmless. All five CTest
checks pass in the preview build.

At this checkpoint LayerShellQt was not yet installed, so the live shell had not
run. It has since run on physical hardware; see the next section.
The lower-level compositor protocols are tested separately as described above.

## First physical session

On 2026-09-24 `--session` ran from tty3 on an Arch laptop (AMD Vega 8 through
amdgpu, 1920×1080 eDP panel, touchpad; an NVIDIA GTX 1650 was present but
unused). libseat opened the seat through logind. The user checked rendering,
touchpad and keyboard input, launching and moving/resizing windows, the shell's
panel and launcher, and an XWayland application (Discord).

Switching to another VT made shaodesk exit. wlroots 0.20 destroys every DRM output
when the session pauses and recreates them on resume; the compositor took the last
output's removal to mean its nested window had closed, and the shell quit when its
last view closed. After the fix, repeated `chvt` round trips kept the compositor,
shell, and clients alive, and the panel returned on the recreated output. Output
removal logs `Failed to disable CRTC` while the DRM FD is paused; it is harmless.

The same day it ran on an Arch desktop: NVIDIA RTX 4090 with the proprietary
610.57 driver (`nvidia_drm.modeset=1`), wlroots 0.20.2 on the GLES2 renderer,
and three monitors (HDMI-A-1 and DP-3 at 2560×1440, DP-1 at 1920×1080). No
NVIDIA workaround variables were needed. All three screens lit at native
resolution; the user checked the cursor crossing between them, keyboard input,
launching and moving windows, and the panel and taskbar on each screen. The
session was started from tty3, since SDDM's Xorg holds tty2 there, and Xwayland
fell back from the `:0` socket SDDM owns.

The test found three problems, now fixed:

- Every monitor marked a 60 Hz mode as preferred, so 144 and 200 Hz panels ran
  at 60 Hz. Outputs now use the fastest refresh at the preferred resolution
  (200/144/144 Hz on this machine), falling back if the driver rejects it.
- `wlr_output_layout_add_auto` placed monitors in connector order, not their
  physical order. The Lua `outputs.order` and `outputs.primary` settings now set
  it; the user confirmed the layout.
- The taskbar's window menu always opened over the first task button.

These runs were launched over SSH into the tty3 login (`XDG_SESSION_ID`), which
left the process outside that logind session: Ctrl+Alt+F1 was refused
(`Could not switch session: Access denied`), and after the compositor stopped,
the console's keyboard stayed unusable until a reboot. Start `--session` from the
console itself. VT switching on NVIDIA therefore remains unverified.

Clients noted that shaodesk lacks xdg-activation, primary selection, fractional
scaling, and server-side decorations. The first three were added later (see the browser
checkpoint below); X11 windows that ask for decorations now get window controls.

Still untested on hardware: VT switching on NVIDIA, hotplug, suspend/resume, lid
close, and brightness/volume keys.

## Tiling checkpoint

Added 2026-09-24. `ctest` covers the dwindle layout on its own (splits, gaps,
bounds, non-overlap, removal, pointer-side placement, split resizing, separate
trees per output and workspace) and in a headless compositor with real clients:
the toggle, splitting on map, refill after moving a tile to another workspace,
`toggle_floating`, restoring floating sizes, and `subscribe` events. The shell UI
test clicks the panel button against a stand-in control socket. Mouse resizing
and drag-to-retile of tiles have not yet been exercised in a nested or physical
session.

## Browser, Electron, and screen capture checkpoint

Added 2026-09-24. In a headless session on the NVIDIA desktop's GLES2 renderer,
Firefox 154 and Discord 1.0.158 (Electron, both `--ozone-platform=wayland` and via
XWayland) mapped, rendered, and appeared in `shaodesk msg get windows`, using
linux-dmabuf and explicit sync. `wl-copy --primary` / `wl-paste --primary` round-tripped
the primary selection. `grim` captured the output (wlr-screencopy), and `grim -T`
captured Firefox's window alone through ext-foreign-toplevel-list and the per-window
capture source. `compositor_smoke` checks that every one of these globals is advertised.

Not yet exercised: screen sharing end to end through xdg-desktop-portal-wlr and
PipeWire, the D-Bus environment export in a physical `--session`, drag-and-drop, pointer
lock, and popup placement at output edges.

## Multi-monitor tiling checkpoint

Added 2026-09-25. `output_tiling_smoke` runs two headless outputs of different sizes
and scales (2048x1152 logical at 1.25, and 1600x900): disabling one in the config moves
its tiles into the other's tiling without overlap, fullscreen then covers exactly the
remaining output, and turning tiling off leaves every window floating inside it. With a
temporary virtual-pointer build (not committed), dragging a maximized window onto the
smaller output tiled it there beside later windows, a tile dropped there stayed inside
it after tiling was turned off, a window floated with `toggle_floating` stayed floating,
and a window straddling both outputs went fullscreen and maximized on the one it mostly
covered. Not yet exercised on physical mixed-scale monitors.

## Per-monitor workspaces checkpoint

Added 2026-09-25. `output_workspace_smoke` runs two headless outputs with real clients:
both start on workspace 1, switching one (by `output NAME` or through the focused output)
leaves the other alone, each window's visibility follows its own output's workspace,
`move_to_workspace` keeps a window on its output, a window placed onto the other output
joins the workspace showing there, and the `subscribe` stream reports each output. The
shell UI test checks the panel's workspace indicator (current and occupied marks, click,
and wheel paging) against a stand-in control socket, and a headless run with the real
shell on two outputs showed each panel marking its own output's workspace (`grim`).
Not yet exercised on hardware: pointer drags between monitors, wheel paging with a real
mouse or touchpad, and the remembered workspaces across a VT switch.

## Screenshot checkpoint

Added 2026-09-25. `config` covers the default Print bindings, the `screenshots` settings, and
invalid modes and directories. `screenshot_smoke` drives `shaodesk msg screenshot` in a headless
session whose `PATH` holds only stand-ins for grim, slurp, wl-copy, and notify-send: it checks
`grim -o` with the output under the pointer, `grim -g` with the focused window's box, the region
from slurp, distinct names for screenshots within one second, saving without wl-copy, and the
errors when grim or slurp is missing. With the real grim in a headless session, the output and
window modes produced a 1280×720 PNG and a 320×240 crop of the probe window. Not yet exercised:
the Print keys and slurp's interactive selection in a nested or physical session, the clipboard
copy, and the notification.

## Window animation checkpoint

Added 2026-09-25. `animation_smoke` runs a headless compositor with tiling and a
1-second duration, and checks through `shaodesk msg get animations` that opening,
the neighbour's glide, and the closing copy each run and then end, that closing copies
leave no scene trees behind, that a reload turning animations off ends those running,
and that quitting mid-animation exits cleanly. Under AddressSanitizer and
LeakSanitizer the compositor exited without findings. `grim` screenshots of a headless
session with foot showed the fade and scale on opening, the glide, and the closing
copy fading out. Not yet seen on a real display: smoothness at 120 ms on high refresh
rates, and GPU clients (dmabuf) closing.

## Panel context menus

Added 2026-09-25. The task menu used a `TapHandler`, which drops a press held past
the long-press time (0.8 s) and one that moves past the drag threshold, so a
right-click could open nothing. Menus now open on press. In a headless session
(pixman and GLES2, output scale 1.25 and three outputs, a floating bar) driven by a
scratch `zwlr_virtual_pointer_v1` client, right-clicking a task showed its menu on
every output, and Maximize / restore, Minimize and Close window acted on foot;
right-clicking empty bar space showed the bar menu, whose tiling and Applications
items worked. `shell_ui` right-clicks a stand-in task (holding the button for a
second) and empty bar space, and checks each menu lies inside the grown surface.
Not yet checked with a physical mouse or touchpad.

## Scrolling layout

Added 2026-09-28. `tiling_tests` checks the column geometry (widths, gaps, stacks, the
three view-follow modes, presets, consuming and expelling, moving columns) and runs 4000
random operations checking that every window is placed once and none overlap, also under
AddressSanitizer and UBSan. `scroll_smoke` drives a headless compositor with three
clients through scroll, width, center, consume and expel actions. Not checked: how the
gliding view looks at real refresh rates, focus following the mouse while columns
move under a still pointer, dragging a column's edge with a physical mouse, Xwayland
windows in columns, or several monitors with different layouts on a real display.

## Output layouts and hotplug

Added 2026-09-28. `tiling_tests` checks that an output's layout defaults sit between the
global defaults and a workspace's own choice. `output_layout_smoke` reloads the configuration
under two headless outputs and checks that `layout.outputs` reaches windows already open
without overriding a layout chosen by an action. `output_hotplug_smoke` unplugs and re-plugs
a headless output (`shaodesk msg headless_output`) holding tiled windows on two workspaces and
floating windows: they move to the other output, keep their workspaces and tiling, and come
back, or stay with `outputs.return_windows = false`. Not checked on real hardware: a monitor
that goes away and comes back through DRM (sleep, cable pull, DisplayPort link retraining),
hotplug events arriving while a client is mid-commit, or fractional scales that differ
between the monitors.

`scroll_follow_smoke` checks the three `layout.scroll.follow` modes (`center`, `edge`, `never`)
against a headless compositor. `output_workspace_move_smoke` moves and swaps workspaces between two headless outputs
(windows, layout state, what each output shows, and an output that does not tile), and checks
that the windows glide. `session_scroll_smoke` saves and restores scroll columns. Not checked:
the look of the glide across two real monitors with different scales or refresh rates.

## Window swallowing, magnetic edges and placement

Added 2026-09-28. `swallow_smoke` runs a headless compositor whose probe client starts
another probe window on a signal, directly or through a shell that stays in between, as an
application started from a terminal is: the window takes the terminal's tile or floating
rectangle, the terminal is hidden and off the taskbar, closing the window brings it back
(also from another workspace, and from fullscreen), `exceptions`, the case-blind terminal
list, a terminal that dies first, `enabled = false` and `swallow_toggle` (both ways) behave as
documented. `swallow_x11_smoke` does the same with an XWayland window, whose process comes from
`_NET_WM_PID`. `magnet_smoke` drags and resizes a floating window through a virtual pointer
and keyboard (`pointer_probe`, with the client asking for the move as a client-decorated window
does): edges land on the output, the area a panel leaves free and another window's edges, hold
until the pointer is the distance away, honour the bypass modifier and the settings, keep
drag-to-top maximizing, and the guide lines' geometry and pixels (grim) are checked.
`window_placement_tests` covers cascade, center and smart placement including 500 random
layouts, and `placement_smoke` the three modes with a panel, a rule's `position`, other
workspaces and tiling. The compositor tests also ran under AddressSanitizer and UBSan.

Not checked: swallowing with real terminals and real applications (foot, kitty, wezterm
or a terminal that keeps one process for several windows), an application that hands its
window to a running instance, how the magnetism feels with a physical mouse or touchpad at
real refresh rates and whether 12 px suits high-density outputs, magnetism across monitors
with different scales, and smart placement when a client changes its size right after
opening.

## Urgent windows

Added 2026-09-28. `urgent_smoke` drives two to five headless probe windows that ask for
attention through xdg-activation (a token without an input serial, the way a browser handed a
link does): the default policy marks them and leaves focus alone; repeated requests and requests
from the focused window change nothing; `get urgent` and the subscription report them in order;
`focus_urgent` takes them oldest first, switches workspace, brings a scratchpad window back, and
does nothing when none is urgent; focusing by the taskbar protocol, closing, and the policies
`focus` (also from another workspace) and `ignore`, changed by reload, behave as documented.
`urgent_border_smoke` checks with grim that the border pulses, holds `urgent_color`, sits inside
the window without a `border_width` and gives way to the focus color, and that animations off
hold the color at once. `xwayland_urgent_smoke` does the same for
`_NET_WM_STATE_DEMANDS_ATTENTION` and the `WM_HINTS` urgency flag set and cleared by an X11
client, and for a window that asked before mapping (`focus = false` rule). `urgent_shell_smoke` runs the shell on the headless compositor: the taskbar's marker
and the overview's frame appear as pixels and go with focus. `shell_ui`, `shell_tasks`,
`shell_task_filter` and `palette` cover the panel's workspace and task markers, the task
model's urgent role and the palette's ordering. The tests ran repeatedly without a failure, and
the suite passed under AddressSanitizer and UBSan (`shell_preview` timed out once in that
parallel run and passed alone, as it did before this work). The pulse redraws borders every 40 ms
for four seconds: ten windows pulsing at once cost the compositor about 40 ms of CPU in those
four seconds (headless, pixman, 20 windows open), and nothing runs once it ends or while idle.

Not checked: real applications asking for attention (a chat client's notification, Firefox or
Chromium opening a link, a terminal's bell through Xwayland, Steam), whether the pulse looks
right on a real display at its refresh rate, how the taskbar's dot reads next to a real icon
theme. An X11 window that asks for attention before it maps is marked only when it opens
without focus (a `focus = false` rule or another workspace); one that opens with focus has it. A token created by a different client than the window it
activates is treated the same as one from the window itself.

## Notifications and on-screen display

Added 2026-09-29. Everything runs headless on a dbus-daemon the tests start (and kill by process)
on a private address; the shell is started with that address, or with none in the older shell
tests, and never sees the real session bus. `notifications_test` (QtTest) covers the model, the
timers (expiry, hover pause keeping the remaining time, replacement while held), replacement by id
and by stack tag, the limits, do-not-disturb with critical notifications passing, dismissal and
default and named actions, `resident` and `transient`, closing by the application, the history
cap and unread count, and 23 cases of body markup (unknown tags dropped, links limited to web and
mail addresses, entities, unbalanced tags, `>` inside a quoted attribute).
`notifications_dbus_test` calls the interface over the private bus: server information,
capabilities, `Notify` with every hint the parser reads (urgency, value, image data, image path,
desktop entry, resident, transient, stack tag), `CloseNotification`, the `NotificationClosed` and
`ActionInvoked` signals, refusal of a second daemon, taking the name when the owner leaves, and
1500 random calls with mistyped hints, odd action lists and short image data, which must neither
crash nor break the card and history limits. `notifications_smoke` runs the compositor and the
shell headless on two monitors: the card appears top right (pixels checked with grim), a click
runs the default action, `CloseNotification` and timeouts remove the card, hovering with a
virtual pointer holds the timer past its timeout, replacement keeps the id, do-not-disturb
silences cards but not critical ones, the display and the cards go to the monitor with the focus
and cards stay on one monitor while they last, the display's pill and the history popover are
found as pixels, a change of a fake backlight (`SHAODESK_SYSFS`, polled) shows the display, turning `notifications.enabled` off and on by reload releases and retakes the
name, and a real `notify-send -w -A` gets its action back. `shell_ui` drives the bell, the
badge, the cards' buttons, close button and default click, and the history's switch and Clear
button through Qt Quick offscreen; `shell_osd` the display's timing, the volume hook (baseline
quiet, changes shown, mute, output switches quiet) and a fake sysfs backlight. The new tests ran
repeatedly without a failure, under AddressSanitizer and UBSan without a report, and with
`-DSHAODESK_NOTIFICATIONS=OFF` the shell builds and its tests pass. A search for a bug found one:
the do-not-disturb switch called a property setter QML could not reach, which `shell_ui` caught.

The display's surface has an empty input region (seen in the Wayland trace), so clicks go through it.

Not checked: a physical backlight and its keys (the reading and the display are tested against a
fake sysfs; the uevent socket that prompts a read on real hardware only has its message filter
tested, and the kernel is expected to send one per change), the volume
keys on a real PipeWire or PulseAudio session (the hook is driven by a stand-in sound server), real
applications' notifications (Firefox, Chromium, Discord, Telegram, Thunderbird; only `notify-send`
and a probe client were used), icons from a real icon theme (the tests have none, so cards show
the stand-in), cards over a fullscreen window and over the lock screen on a real display, the look
at high scales and with other fonts, a card pointer-hovered on real hardware, and whether the
timings feel right. Another notification daemon started before the shell keeps the bus name; the
shell then logs it and hides its bell, which is only checked with a second in-process daemon.


## Daily use on Gentoo

Added 2026-10-05. shaodesk is the author's everyday desktop on Gentoo: `--session` started from
a TTY, installed under a user prefix (`~/.local/shaodesk`) with the shell, running
`~/.config/shaodesk/init.lua`. The machine is an Intel i9-12900K with an NVIDIA RTX 4090 on the
proprietary 595.99.02 driver, kernel 6.18, wlroots 0.20.2 and Qt 6.11.2, driving three monitors:
HDMI-A-1 at 2560×1440 and 144 Hz and DP-3 at 2560×1440 and 200 Hz, both at scale 1.25, and DP-1
at 1920×1080 and 144 Hz at scale 1 (mixed scales side by side). Problems found in daily use have
been fixed on `main` as they came up; none are known to be open.

This replaces the earlier note in [gentoo.md](gentoo.md) that the Gentoo build had only run in a
VM. Daily use is not a targeted test, so the hardware items listed as not checked in the
sections above stay unconfirmed until someone checks them on purpose: suspend and resume, lid
close (this machine has none), monitors unplugged or woken through DRM, VT switching on NVIDIA,
and brightness keys.
