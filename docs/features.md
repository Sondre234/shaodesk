# Features

What shaodesk does, area by area. The default key bindings are in the [README](../README.md#default-bindings),
and every Lua setting, with its type, default and range, is in the
[configuration reference](config-reference.md).

## Shell

The shell has pinned desktop shortcuts (double-click to launch), a taskbar with
window activation/minimization (clicking a window's button focuses it, or minimizes it when it
is focused already, as on Windows, from any monitor's bar) and a right-click window menu (maximize/restore,
minimize, pin to taskbar, close), middle-click to close a window, a right-click menu on empty bar space (tiling,
applications, show desktop, and the appearance profiles in a submenu), an application
search menu, a workspace indicator, a wallpaper picker, a [system tray](#system-tray), Quick
Settings (the network, the volume, the battery, tiling for its monitor, the appearance profiles,
night light and do-not-disturb), a clock, and a show-desktop button. Buttons show a tooltip
on hover. Installed applications are read
from desktop entries through GIO. Lua configures the panel's height, top or
bottom placement (`panel_position`), margins that make it float (`panel_margin`, one
number or `{ top, right, bottom, left }`), corner radius, font and text size, colors
(`#RRGGBB`, or `#RRGGBBAA` for a translucent panel), wallpaper, and pinned commands. Pinned commands run from your home directory. Installed applications can also be
pinned to the taskbar from a window's menu or the application menu, and unpinned
by right-clicking their button; those pins are kept in
`$XDG_STATE_HOME/shaodesk/pinned` (`~/.local/state/shaodesk/pinned`), one desktop id per
line, and stay off the desktop. An application's windows share one taskbar button, stacked
with a count when there are several: clicking it cycles through them, and hovering lists them
to pick or close one. `group_windows = false` gives every window its own button. The workspace indicator lists the monitor's workspaces (click one to switch, scroll to page
through them). The battery shows the charge of the first battery in `/sys/class/power_supply`
(red when nearly empty, accent-coloured while charging) and is left out on machines without
one; the network icon shows Wi-Fi, a wired link, or a dimmed struck-through icon when the
interface is down, read from `/sys/class/net` (physical interfaces only, wired preferred), and
is left out without any interface. Both refresh every five seconds. With two or more
[keyboard layouts](#keyboard-layouts), the active one's short name (`us`, `no`) sits beside the
clock, and clicking it switches every keyboard to the next. Beside the time, the clock shows
how many notifications have not been seen, muted (and a crossed-out bell) while do-not-disturb is
on, which a right-click on it toggles. Clicking the clock opens its flyout at the bar's right end,
as on Windows 11: the [notifications](#notifications-and-on-screen-display) on one card, and under
it a calendar with the time and today's date over the month, today marked and the weeks starting
on the locale's first day. The arrows or the mouse wheel page through the months, each sliding
in; the title zooms out to the year's months and then to a decade's years, where a pick zooms back
in; Today returns to this month.

Left of the clock, as on Windows 11, the Quick Settings button shows the network, the volume and
the battery in small icons; its wheel changes the volume and a middle click mutes. Clicking it
opens Quick Settings at the bar's right end: tiles for do-not-disturb, night light (on or off
against its schedule, through the compositor), tiling on this monitor, the appearance profile
(listing the profiles under it), the wallpaper (opening the same picker as the bar's button) and
the network (its state only: Wi-Fi, wired or down, and the interface; shaodesk does not manage
connections); a brightness slider where the screen has a backlight (set through logind's
`SetBrightness`, so no privileges are needed); the volume with its mute, the outputs to play
through and each application's volume a click away; and the battery's charge along the foot.

Each widget is switched off from Lua: `shell = { widgets = { battery = false, calendar = false } }`,
with `workspaces`, `battery`, `network`, `volume`, `clock`, `calendar` (the clock stays, the
calendar goes), `tiling`, `profiles` (the appearance profile picker), `wallpapers`,
`notifications` (do-not-disturb), `keyboard_layout`, `power` (the [power button](#power) in the
application menu) and `tray` (the [system tray](#system-tray)) all on by default. Those that can
move sit on the bar with `"bar"` or in Quick Settings with `"quick"`, and `true` leaves them in
their default place: `network`, `battery`, `volume`, `tiling`, `profiles` and `notifications`
are in Quick Settings and `wallpapers` on the bar. Placed on the bar, each has its button there
as before (`notifications = "bar"` is a bell with the unread count, which the clock otherwise
stands for), and the Quick Settings button goes once nothing is placed in it:

```lua
shell = { widgets = { volume = "bar", network = "bar", battery = "bar", tiling = "bar",
                      profiles = "bar", notifications = "bar", wallpapers = "quick" } },
```

All of them take the panel's `accent`,
`panel_color`, `text_color`, `font` and `font_size`: hovering, pressing and what is open or on
are shown by laying the text color over the panel at a low opacity, which shows on a light
panel as on a dark one. Popups (the application menu, menus, the mixer, the calendar and the
rest) are drawn in the panel color made opaque, even when the bar is translucent: nothing is
blurred behind them, and a window showing through would make them hard to read. They open in a
surface of their own over the whole monitor, above fullscreen windows too, so the application
menu Super + R opens shows over a video; they fade in with a short slide from the bar and fade
out (instant with `animations.enabled = false`), and cast a soft shadow when the shell draws
through the GPU. While one is open, the keyboard is in it and a press anywhere but on the bar or
the popup closes it without reaching what is under it; a press on another bar button opens that
one's popup at once. The overview, the window switcher and the command palette close it. Menus
(the bar's, a window's, the tray's, the power menu) take the keyboard: Up and Down (wrapping),
Home and End move, Enter or Space chooses, Right opens a submenu and Left or Escape closes it,
and Escape closes the menu; resting the pointer on an entry with a submenu opens it beside the
entry, the menu staying open. The shell
draws through the GPU; `renderer = "software"` draws on the CPU instead, for a weak machine: it
starts faster and uses less memory, but cannot draw effects such as shadows. In a nested
session, applications that reuse an existing process or D-Bus service can open
in the host session instead.

## Windows

### Decorations, focus and fullscreen

Windows that leave decorations to the window manager (Wayland applications that support
server-side decorations, such as kitty, and X11 applications such as Spotify) get no title
bar. Instead, a strip of three flat buttons sits over their top-right corner (minimize,
fullscreen, and close, from left to right) and appears when the pointer nears that corner, so
it never covers text. Dragging the window's top edge (its top 6 pixels, as a title bar would;
`windows.drag_strip` changes how many) moves the window. Other windows, and windows that ask to draw their own frame, decorate
themselves.

Dropping a dragged window with the pointer at the top edge of the screen, or on a panel along
it, maximizes it below the panels; dragging it away again restores its earlier size. Hovering a
window focuses it without raising it (`mouse.focus_follows = false` turns this off), except
while dragging, while a menu or popup is open, or while a panel or launcher has the keyboard.

A window you fullscreen with a binding or its title bar fills its monitor except the panel and
other bars, which stay shown. Fullscreen a program asks for itself, like a video player or a
browser's fullscreen video, covers the whole monitor, panels included, and keeps covering it
while you work on another monitor; bringing another window forward on its monitor puts it
behind that window and shows the panels again.

Limitations: snapping is keyboard-driven, without edge-drag previews. Window
placement during interactive resize is immediate, without waiting for the
client's next buffer.

Actions without a section of their own: `quit` ends the session (Super + M), and `snap_left` and
`snap_right` fill half of the monitor with the focused floating window (no default binding).
`focus_last` (Super + `) focuses the window focused before this one, on any workspace; repeated,
it flips between two windows.

### Borders, opacity and rounded corners

`layout.gap` sets the space around tiles; `gap_inner` (between windows) and `gap_outer`
(at the output's edges) set them separately. Hyprland's `gaps_in` is half of `gap_inner`,
since Hyprland adds it on both sides. The `windows` table draws a border around each window
(`border_width`, `border_color` for the focused one, `border_inactive_color`) and sets
`opacity` and `inactive_opacity`, per application too with `rules`. Fullscreen windows have no
border and stay opaque. Windows on a monitor with tiling on (floating ones too) and their
border get rounded corners (`corner_radius`, 10 by default, 0 for square ones) when wlroots is
built with `packaging/patches/wlroots-rounded-corners.patch`; stock wlroots keeps them square.
Blur and shadows need a renderer that wlroots' scene graph does not provide.

### Window rules

Each entry of `windows.rules` matches windows by `app_id`, `title`, or both, each a regular
expression searched in the window's app ID or title (X11 windows match their `WM_CLASS` class
as `app_id`). A window takes its `opacity` and `inactive_opacity` from the first matching rule
that sets either. Rules can also act on a window once, as it opens (like sway's `for_window`
and `assign`):

```lua
windows = {
    rules = {
        { app_id = "^org.pulseaudio.pavucontrol$", floating = true, size = { 800, 500 },
          position = "center" },
        { app_id = "^thunderbird$", workspace = 2, focus = false },
        { title = "^Picture-in-Picture$", floating = true, size = { 480, 270 },
          position = { 1400, 40 }, focus = false },
        { app_id = "^mpv$", output = "HDMI-A-1", fullscreen = true },
    },
},
```

| Action | Effect |
| --- | --- |
| `floating = true` / `false` | Keeps the window out of the tiling, or tiles it even if it is a dialog. |
| `workspace = N` | Opens it on workspace N of its monitor, without switching there or focusing it. |
| `output = "NAME"` | Opens it on that monitor: a connector name, or `"desc:"` and the start of its "make model serial", as in `outputs.monitors`. A monitor that is not connected is ignored. |
| `size = { w, h }` | Its floating size (also `{ width = w, height = h }`), at most the usable area. |
| `position = "center"` or `{ x, y }` | Its floating place, centred or from the top-left of the monitor's usable area (panels excluded; also `{ x = x, y = y }`). |
| `fullscreen = true` | Opens it fullscreen. |
| `maximize = true` | Opens it maximized, floating over the tiles. |
| `focus = false` | Leaves the focus where it was. A fullscreen window on the current workspace still takes it. |
| `sticky = true` | Opens it sticky (see below) on its monitor's current workspace, whatever `workspace` says. Ignored with `features = { sticky = false }`. |

Every matching rule applies, in order; where two set the same action, the later one wins. A
tiled window keeps `size` and `position` as the place it floats to when toggled. Rules see the
app ID and title the window has when it opens; later title changes do not apply them again.
`features = { window_rules = false }` turns the actions off and leaves opacity rules working.

### Window placement

`windows.placement` chooses where a new floating window opens (a tile goes where the layout
puts it, and a window rule's `position` wins over all of these). `"cascade"`, the default, steps
each window 32 pixels down and right from 40 pixels in, one step for every window already open,
wrapping after eight. `"center"` opens it in the middle of the area the panels leave free on
the pointer's monitor. `"smart"` looks for the place where it covers the other windows on that
monitor and workspace least: it tries the positions flush against the screen's and the windows'
edges and in the middle of each gap between them, takes the one that covers nothing with the
most room around it (a window beside another lands in the middle of the free part of the
screen, the first window in the middle of the screen), and cascades when there is no free space
for it. Windows on other workspaces and monitors, and hidden ones, do not count. The choice is
made from the size the window asked for when it opens; the algorithm has unit tests
(`tests/window_placement_tests.cpp`) and the modes are checked headless with a panel
(`tests/placement_smoke.py`).

### Magnetic edges

A floating window that is dragged (Alt + left drag, or a client-decorated window's own title
bar) or resized (Alt + right drag, or its border) sticks to nearby edges, so windows line up
without pixel hunting. Within
`windows.magnet.distance` (12 px by default) an edge of the window lands on the edge of the
output, of the area the panels leave free, or of another window, and a translucent guide line
(`guide_color`) is drawn along the edge that holds it, from one window to the other. It works
per axis (a corner can catch both), and against a window only where the two overlap in the other
direction, so a window far above or below does not pull. The window follows the pointer's own
position, so it stays held until the pointer has moved the distance away, then follows it again;
hold the `bypass` modifier (Shift by default; `"none"` for no modifier) to drag freely.
`windows.magnet = { enabled = false }` turns it off, and `guides = false` leaves the lines out.
Tiles and windows lifted out of the tiling to be dropped somewhere are not affected, and
dropping a window at the top of the screen still maximizes it (that goes by the pointer). When
resizing, only the edges being pulled stick. It does not act on keyboard moves or resizes, or on
snap actions. Tested headless with a virtual
pointer and keyboard, including the guide's pixels (`tests/magnet_smoke.py`); the feel of the
distance with a real mouse has not been judged.

### Window groups

A group puts several windows in one slot, a tile or a floating place, and shows one at a time.
`group_toggle` (Super + G) makes the focused window a group of one; with `features.group_join_new`
(on by default) every window that opens while a group has focus joins it as a new tab and takes
its slot. `group_next` and `group_prev` (Super + ] and [) step through the tabs, wrapping, and
`ungroup` (Super + Shift + G) takes the focused window out into a tile of its own beside the
group. `group_merge_left`, `group_merge_right`, `group_merge_up` and `group_merge_down` move the focused window into the group of
the window beside it in that direction (making that window a group if it is none), where it
becomes the shown tab; they have no default binding. `group_toggle` inside a group dissolves it:
the hidden windows tile beside the shown one again. Closing the shown window shows the next tab,
and a group left with a single window dissolves.

The group's windows are drawn as a strip of tabs, a thin bar over the top edge of the shown
window, one segment per member with the shown one lit. A click on a segment brings that window
forward. The strip carries no titles: the compositor has no text rendering, so the taskbar (which
lists every member, and switches tabs when a hidden one is activated) and the window switcher
name the windows. `shaodesk msg get windows` ends each line with the window's group number (0 for
none). Hidden members are off the screen, in no tiling, and follow the shown window to another
workspace or monitor. `features.groups = false` turns the actions off and dissolves every group
on reload. `session save` keeps only the shown window of a group, not the group.

### Urgent windows

An application that is not focused can ask for attention: a chat client with a new message, a
terminal's bell, a browser opening a link from another program. Wayland clients do it with
xdg-activation, X11 clients with `_NET_WM_STATE_DEMANDS_ATTENTION` or the urgency flag of
`WM_HINTS`. `windows.activation` decides what happens:

| value | effect |
|-------|--------|
| `"urgent"` (default) | The window is marked urgent and focus stays where it is, so nothing steals it. |
| `"focus"` | The window is focused, switching to its workspace, as before this setting existed. |
| `"ignore"` | The request is dropped. |

An urgent window's border pulses for four seconds in `windows.urgent_color` (orange by default)
and then holds it, also on windows without a `border_width`, where a two pixel frame is drawn
inside the window's edge without moving anything. Its taskbar button (or the stack it is in) gets
a tinted background and a pulsing dot, the workspace indicator marks its workspace, the overview
frames its thumbnail and colours its workspace, the window switcher puts a dot on its icon,
and the command palette lists it first with "needs attention". `focus_urgent` (Super + U, `shaodesk msg focus_urgent`) focuses the window that
has been urgent the longest, switching workspace and showing it if it was minimized or in the
scratchpad; a window that gets focus any other way, or closes, is no longer urgent. The focused
window is never urgent, and a client that clears its X11 hint or demand ends it too.

`shaodesk msg get urgent` prints the urgent windows, the longest waiting first, in the columns of
`get windows`. Subscribers to the control socket receive `urgent COUNT` in every state, then an
`urgent-output NAME 1,3` line per monitor with the workspaces holding urgent windows and an
`urgent-window OUTPUT WORKSPACE APP_ID TITLE` line (tab-separated after the name) for each of the
first sixteen, so a script or a panel can follow them without polling. The shell finds a task
by app ID and title, which the foreign-toplevel protocol has no urgent state for; two windows of
one application with the same title are told apart only by their order.

### Window swallowing

With `windows.swallow = { enabled = true }` a window started from a terminal takes the terminal's
place and hides it, as in Hyprland and dwm's swallow patch: run `mpv clip.mkv` or `imv photo.png`
in a terminal and the player opens in the terminal's tile (or floating rectangle, size and
place), and closing it brings the terminal back there, with focus. It is off by default.

The match is by process ancestry, not by guessing: the new window's process (its Wayland
client's pid, or the X11 window's) has to descend from the process of a terminal's window, however many shells and
launchers lie between, read from `/proc`. Only windows whose `app_id` (X11 class) is in
`windows.swallow.terminals` (compared without regard to case; by default kitty, foot, footclient,
Alacritty, wezterm, ghostty, xterm, URxvt, konsole and GNOME Terminal) can be swallowed. A window that is
itself a terminal never swallows (so a terminal started from a terminal opens beside it), nor
does a dialog, an `app_id` in `windows.swallow.exceptions`, or a window a rule sends elsewhere
(floating, another workspace or monitor, sticky). When a process has several windows (a
`foot --server`), the focused one, else the last focused, is the one swallowed. A terminal that is minimized, in the
scratchpad, sticky or in a group is left alone.

A swallowed terminal is hidden, leaves the taskbar, the window switcher and the overview, and
follows its window to another workspace or monitor; the window is an ordinary one (it can be
floated, moved and resized), and the terminal returns to wherever it is when it closes. If the
terminal closes first, the window keeps the place. `swallow_toggle` (no default binding; it
works with `enabled = false` too) does it by hand: pressed on a window that swallowed a terminal it
gives the terminal a place beside it again, otherwise the window takes the place of the terminal
it was started from, or of the terminal last focused on the workspace. `shaodesk msg get swallow`
lists each window with whether it is swallowed and its partner. Only applications that stay
descendants of the terminal can be swallowed: one that hands the request to a running instance
(a browser opening a new window) belongs to the other process. This is checked headless with
process ancestry through a shell, and with an X11 window (`tests/swallow_smoke.py`,
`tests/swallow_x11_smoke.py`); with real terminals (foot, kitty) it has not been.

## Window switcher

Alt + Tab (`switcher`) opens the window switcher in the middle of the focused monitor. It
lists every window, on every monitor and workspace and including minimized ones, most
recently focused first, with the window focused before the current one selected. Keep Alt
held and press Tab to move on (Shift + Tab, `switcher_prev`, or the arrow keys, to move
back); releasing Alt focuses the selected window, switching its monitor to its workspace and
restoring it if minimized. Return picks at once, Escape cancels, and clicking a window picks
it. While the switcher is open, keys do not reach applications. The compositor keeps the
switcher and the shell draws it, only once it has been open for a moment, so a quick
Alt + Tab flips between two windows without it flashing up; without the shell it still
switches, unseen. Bound to a key without modifiers, or sent as
`shaodesk msg switcher`, it stays open until `switcher_confirm [N]` (the Nth window in the
list), `switcher_cancel`, Return, or Escape. `cycle` is the older action that raises the
least recently focused window on the spot.

## Overview

Super + O (`toggle_overview`) shows the focused monitor's workspace as an Exposé: every
window as a live, scaled thumbnail in a grid that fits them as large as possible (never larger
than the window itself) and keeps each one's proportions, with a strip of the monitor's
workspaces above it. The compositor draws it from scaled copies of the windows' own scene
nodes, so the thumbnails move with the windows' contents (video, a terminal) and cost no more
than a few scene nodes each; the shell draws only text over it. The thumbnails glide out of
the windows' places when it opens and back into them when it closes. It changes nothing until
you pick something:

- Type to filter by title or application (several words must all match; the grid then lists
  matching windows from every workspace and minimized ones). Backspace deletes, Escape clears
  the filter, and a second Escape closes the overview.
- The arrow keys move the selection through the grid (Tab and Shift + Tab step through it,
  Home and End jump to the ends). Return, or a click on a thumbnail, focuses that window,
  switching to its workspace if needed. The mouse selects by pointing.
- Delete, or a middle click on a thumbnail, closes that window.
- The strip shows each workspace with its windows; Page Up and Page Down, Ctrl + Left and
  Right, or the mouse wheel show another workspace in the grid without switching to it. A
  click on a workspace in the strip shows it; a second click, or Return on an empty one,
  switches to it. Dragging a thumbnail onto a workspace in the strip moves the window there.
- Escape, Super + O again, or a click on the backdrop closes it without changing anything.
- Placing the pointer in a screen corner opens it (`overview.hot_corner`; the general
  `hot_corners` table can bind `toggle_overview` to a corner too, with a dwell time).

Keys and the mouse do not reach applications while it is open. Fullscreen windows are lowered
under it, so it opens over them. Settings are in the `overview` table (`enabled`, `gap`,
`animation`, `duration`, `strip`, `hot_corner`, `dim`; see the [configuration reference](config-reference.md)).
Scripts can drive it: `shaodesk msg toggle_overview`, `overview_confirm [N]`, `overview_cancel`,
`overview filter TEXT`, `overview select N`, `overview view N` (from 1), and `get overview`,
which lists the state, the selection and the rectangle of every thumbnail and strip cell.
Subscribers get `overview` lines (see `overview_describe` in `src/compositor/overview.c`) as
it opens and changes.

## Command palette

`palette` (Super + P, or `shaodesk msg palette`) opens a search box in the shell, on the monitor
under the pointer, that reaches everything from one place: open windows, installed applications,
workspaces (switching to one, or moving the focused window there), compositor actions such as
Open a terminal (`terminal`), `layout_monocle` or `group_toggle`, the [power actions](#power) that may run (Restart, Power off
and Log out asking first, as from the panel), and saved sessions (restore, restore and launch what is
missing, and save the current arrangement under the name typed). Type to narrow the list; each
word must match, in any order, as letters in sequence of the title or its small print, favouring
runs of letters and word starts (`gc` finds Google Chrome, `lay mon` finds Layout: monocle).
Up and Down, Tab and Shift + Tab, Ctrl + N and Ctrl + P, or the pointer select; Enter or a click
runs the entry; Escape, or clicking elsewhere, closes it. A leading `>` searches only actions, `@`
windows, `#` workspaces and `%` sessions. The palette needs the shell (`shaodesk-shell`), which
draws it, and the compositor's control socket for actions and sessions. On a machine with 600
entries a keystroke re-ranks them in under a millisecond (an optimized build; see
`tests/palette_test.cpp`). Super + P used to toggle sticky windows; that moved to Super + Shift + P.

## Terminal

The `terminal` action (`shaodesk msg terminal`) opens a terminal: the program and arguments of
the `terminal` setting when there is one (`terminal = { "foot" }`), else `$TERMINAL` when it
names an installed program, else the first of kitty, foot, alacritty, wezterm, ghostty,
konsole, gnome-terminal and xterm found on `PATH`. A configured terminal that is not installed
is an error rather than a reason to open another. When no terminal is found, or the one chosen
cannot start, the panel says why for eight seconds, as it does for any program a binding, a hot
corner or the command palette cannot start. Each of these terminals is in
`windows.swallow.terminals` by default, so [swallowing](#window-swallowing) works with whichever
opens; another terminal needs its app ID added there. `tests/launch_smoke.py` checks the choice
with stand-in programs.

## Workspaces

Every monitor has its own workspaces, numbered 1 to `layout.workspaces` (1–10), and all
start on 1. Workspace shortcuts switch the focused monitor: the one whose window was
focused, whose workspace was switched, or that was clicked last. A monitor's bare desktop
counts as a window there: pointing at it (with focus following the mouse) or clicking it
focuses that monitor, and the window on the other monitor loses the keyboard, so Super + minus
or Super + R then act on the empty monitor. A window belongs to the
monitor it is on; moved to another monitor, it joins the workspace showing there. The
panel on each monitor shows that monitor's workspaces, marking the current one and those
with windows; scrolling over it pages through them and clicking a number switches to it.
`layout.workspace_names = { "web", "code" }` labels workspaces 1, 2, ...: the panel shows the name
instead of the number, and `workspace` and `move_to_workspace` accept it, in bindings
(`workspace = "web"`) and from the control socket (`shaodesk msg workspace web`; a name with spaces
is written as is).
The taskbar lists windows from every workspace, and activating one switches its monitor
to its workspace. Window shortcuts act only on visible windows.

Each monitor also remembers the workspace it showed before, however it was switched, and
the `workspace_back` action (Super + Tab) returns to it, like sway's
`workspace back_and_forth`. With `features = { workspace_back_and_forth = true }`, as sway's
`workspace_auto_back_and_forth`, a `workspace N` action for the workspace already shown
does the same, so pressing Super + 2 twice flips between workspace 2 and the one before.
It is off by default.

`workspace_prev` and `workspace_next` (Super + Ctrl + Left / Right) step to the previous and next workspace.

### Scratchpad

The scratchpad works as in sway. `move_to_scratchpad` (Super + Shift + minus) floats the
focused window and hides it. `scratchpad_show` (Super + minus) hides the focused scratchpad
window again; otherwise it focuses a scratchpad window already shown on the focused monitor, or
brings the one hidden longest to the middle of that monitor's current workspace, so repeated
presses cycle through them. A shown scratchpad window stays in the scratchpad until it is moved
to a workspace or tiled (Super + V). Hidden windows stay in the taskbar as minimized windows,
so the mouse can still find them; activating one there shows it as `scratchpad_show` would.
`features = { scratchpad = false }` turns both actions off, and a reload then brings hidden
windows back to the current workspace.

### Sticky windows

A sticky window, as with sway's `sticky enable`, shows on every workspace of its monitor:
`toggle_sticky` (Super + Shift + P, `shaodesk msg toggle_sticky`, or a mouse button binding for the
window under the pointer) floats it, and switching that monitor's workspace keeps it shown
and raises it over the workspace's windows. Moved to another monitor, it stays sticky there;
`move_to_workspace` unsticks it and moves it. Unsticking a window that was a tile tiles it
again on the current workspace (Super + V does the same). The taskbar keeps listing it, and
activating it does not switch workspaces. `features = { sticky = false }` turns the action
off, and a reload that sets it returns sticky windows to their monitor's current workspace.

## Tiling

Tiling is a setting of each monitor. The tiling button on a monitor's panel (next to the
clock) switches that monitor between floating windows and automatic tiling; Super + S
or `shaodesk msg toggle_tiling` switches the focused monitor, and
`shaodesk msg output HDMI-A-1 toggle_tiling` a named one. Lua `layout.tiling = true` starts
every monitor tiled, and `tiling` in a monitor's `outputs.monitors` entry overrides it:

```lua
layout = { tiling = false },
outputs = { monitors = { ["DP-3"] = { tiling = true } } }, -- only DP-3 tiles
```

With `layout.tiling_per_workspace = true`, toggling switches only the workspace the monitor
shows: the others keep their own state, and workspaces never toggled follow the monitor's
setting. A window moved to another workspace tiles or floats as that workspace does, and
swapping workspaces between monitors takes their tiling state along. Turning the setting off
again puts every workspace back on its monitor's setting.

A monitor keeps its toggled state across reloads, and across being unplugged, until its
configured setting changes; a reload then applies the new setting. Tiling follows Hyprland's default *dwindle*
layout: every output and workspace has its own binary split tree, each split divides
its space along the longer side, and a new window opens on the output under the
pointer, splitting the focused window there (or the one under the pointer) on the side
nearer the pointer. Floating windows also open on the pointer's output. Closing a window gives its
space back to its neighbour.

- Mod + right drag on a tile, or dragging its edge, moves the split lines around it.
- Moving a tile (Mod + left drag or its title bar) lifts it out; dropping it splits
  the tile under the pointer. A window dropped on another monitor that tiles joins the
  tiling there even if snapping or maximizing, or its old monitor not tiling, had floated
  it; one floated with Super + V stays floating. A tile dropped on a monitor that does not
  tile floats there.
- Dialogs and fixed-size windows float. Super + V (`toggle_floating`) floats
  or tiles the focused window; snapping or maximizing a tile also floats it. A window
  opening into the tiling of a workspace brings the windows maximized there back into the
  tiling instead of covering them, as it does fullscreen ones, and turning tiling on brings
  back every window that floated only because it was snapped or maximized
  (`tests/maximized_tile_smoke.py`).
- Turning tiling off returns every window of that monitor to its floating position and
  size. A window
  now tiled on another monitor keeps its size and its place relative to that monitor,
  shrunk and moved to fit inside it.
- Minimized windows leave the tiling and rejoin it when restored; windows moved to
  another workspace join that workspace's tiling on the same output.
- Super + arrows focus the neighbouring tile that way, or the next monitor that way when
  there is none, even an empty one, where new windows then open; Super + Shift + arrows swap
  the focused tile with it (`focus_left` … `focus_down`, `move_left` … `move_down`). With
  `focus_follows` on, these and Alt+Tab put the pointer just inside the window's bottom-right
  corner, out of the way.
- Super + Ctrl + Shift + arrows (`resize_left`, `resize_right`, `resize_up`, `resize_down`) resize from the keyboard, by
  40 pixels or the binding's `amount` (`shaodesk msg resize_right 80`). On a tile the arrow
  moves a split beside it that way: the one on that side of the tile if there is one, growing
  it, else the one on its other side, shrinking it. With two tiles side by side, Right always
  moves the line between them right. A tile in the middle of three columns only grows this
  way; shrink it by resizing a neighbour. Splits stop at 10% and 90% of their space, as with
  the mouse. A floating (or sticky) window moves its right or bottom edge that way, growing no
  further than its monitor's edge and shrinking no smaller than 64 pixels; a snapped window
  leaves its snapped place at its current size. Maximized and fullscreen windows do not
  change. Holding the keys keeps resizing, at the keyboard's `repeat_delay` and
  `repeat_rate`. `features = { keyboard_resize = false }` turns the actions off.

Not yet: keeping floating windows above tiles. Windows tiled on a
monitor that is disabled in the config hands its tiles to the nearest one, where they float if
that one does not tile. Unplugging a monitor does the same with all its windows (floating ones
keep their relative place, tiles join the tiling of the nearest monitor, workspace numbers are
kept), and with `outputs.return_windows` (on by default) they go back to the same workspace and
into its tiling when the monitor is plugged in again, unless it is turned off or the windows were
put elsewhere by hand. With no other monitor left, windows stay where they are.

### Workspaces between monitors

`move_workspace_to_output` sends the focused monitor's current workspace to another monitor:
its windows, and the layout, master ratio and count, splits or scroll columns it had, all
travel, and the other monitor shows it. The workspace with the same number there takes its
place, so nothing is merged or lost. `swap_workspaces` trades all workspaces of two monitors, and
with them what each one shows. Both take a target: `left` or `right` (the neighbouring monitor
that way), `next` or `prev` (left to right, wrapping), a connector name, or `desc:` and the start
of "make model serial". Bind them with `output = ...` (`swap_workspaces` defaults to `next`), or
run `shaodesk msg move_workspace_to_output left`, `shaodesk msg output DP-1 swap_workspaces DP-2`.
Super + Ctrl + comma / period are bound to the first with `left` / `right`; `swap_workspaces` has
no default binding.

Floating windows keep their place relative to the monitor's usable area and glide across;
tiles glide to their new places. On a monitor that does not tile, the arriving tiles float, and
floating windows sent from one that does not tile join the tiling. Sticky windows and the
scratchpad stay with their monitor.

### Tiling layouts

Dwindle is the default, but every output and workspace can tile with another layout, chosen
with `layout_next` / `layout_prev` (Super + Space, Super + Shift + Space), which cycle
dwindle, master, spiral, monocle, scroll, or by name with `layout_dwindle`, `layout_master`,
`layout_spiral`, `layout_monocle` and `layout_scroll` (unbound by default; `shaodesk msg layout_master` works
too). `shaodesk msg get layout` prints the focused monitor's current workspace as `NAME RATIO
MASTER_COUNT`.

- **master**: the first `master_count` tiles share a column on the left, `master_ratio` of
  the width; the rest are stacked in a column beside it. With no more tiles than masters,
  the master column takes the whole width.
- **spiral**: each tile takes `master_ratio` of the space the earlier ones left, turning
  clockwise (left, top, right, bottom, ...), the last tile getting what remains.
- **monocle**: every tile fills the area, the focused one on top; Super + J / K (`focus_next`
  / `focus_prev`) or the arrows step between them, and Alt + Tab lists them as usual.

- **scroll**: see [Scrolling layout](#scrolling-layout).

The layouts other than dwindle order the tiles as a list (the dwindle tree read left to
right): a new window joins the end wherever it is opened or dropped, and Super + Shift +
arrows trade places with the next or previous one in the list rather than the geometric
neighbour. `promote` (Super + Return) swaps the focused tile with the first, or with the
second when it is the first; `swap_next` / `swap_prev` (Super + Shift + J / K) trade with the
next or previous; `focus_next` / `focus_prev` step focus in order, wrapping, in every layout.
`master_grow` / `master_shrink` (Super + L / H) move the ratio by 5% of the width and
`master_more` / `master_less` (Super + comma / period) add or remove a master, from 1 to 8;
in master, `resize_right` / `resize_left` and dragging the boundary move the ratio too. A
ratio stays between 10% and 90%. Switching layouts never loses the dwindle tree: swaps
stay, but its splits return exactly as they were.

```lua
layout = {
    tile_layout = "master",  -- what workspaces start with: dwindle, master, spiral, monocle, scroll
    master_ratio = 0.55,     -- 0.1 to 0.9
    master_count = 1,        -- 1 to 8
},
```

A workspace keeps the layout, ratio and master count it was given until the compositor
restarts; a reload changes the starting values only for workspaces that have not chosen
one.

Each monitor can start with its own values in `layout.outputs`, keyed by connector name or
`"desc:"` and the start of "make model serial" as in `outputs.monitors`:

```lua
layout = {
    outputs = {
        ["DP-1"] = { tile_layout = "scroll" },
        ["desc:Dell U2720"] = { tile_layout = "master", master_ratio = 0.6, master_count = 2 },
    },
},
```

These apply to the monitor's workspaces that were not set by hand (a layout chosen with an
action, or a ratio changed, is kept), also to windows already open when the configuration is
reloaded, and again when the monitor is plugged back in. `shaodesk msg get layout OUTPUT
[WORKSPACE]` prints another monitor's or workspace's values.

### Scrolling layout

`layout_scroll` tiles like niri and PaperWM: windows live in columns on a strip much wider
than the screen, and the screen is a view onto it that follows focus, gliding like any
other tile move. A new window opens in a column of its own right of the focused one. A
column has a width, a share of the screen (`layout.scroll.width`, half by default), and
stacks its windows evenly. Beside its own actions, the usual keys work: `focus_left` /
`focus_right` step between columns and `focus_up` / `focus_down` within one, `move_left` /
`move_right` move the focused column along the strip and `move_up` / `move_down` trade
places within a stack, `focus_next` / `focus_prev` step through every window in order,
`master_grow` / `master_shrink` (Super + L / H) and dragging a column's side resize it,
and the Alt + Tab switcher brings any window into view.

| Action | Does |
| --- | --- |
| `scroll_left`, `scroll_right` | focus the column to the left or right (the view moves to show it, even with `follow = "never"`) |
| `column_cycle_width` | step the focused column through `presets` (a third, half, two thirds, full), wrapping |
| `column_widen`, `column_narrow` | change its width by `step` (10% of the screen), from 10% to 100% |
| `consume_left`, `consume_right` | stack the focused window into the column on that side; a column left empty disappears |
| `expel` | move the focused window out of a stack into a column of its own on the right |
| `center_column` | center the focused column in the view |

```lua
layout = {
    tile_layout = "scroll",
    scroll = {
        follow = "center",                  -- center: always centered; edge: only as far as
                                            -- needed; never: only scroll actions, new windows
                                            -- and center_column move the view
        width = 0.5,                        -- width of a new column, 0.1 to 1
        step = 0.1,                         -- column_widen / column_narrow
        presets = { 1/3, 1/2, 2/3, 1 },     -- column_cycle_width; up to 8 widths
    },
},
```

With `mouse.focus_follows` on, columns that scroll under a still pointer can take focus on
its next move; keyboard-driven scrolling takes the pointer along, but if the mouse causes
trouble, use `follow = "edge"`, which moves the view least.

The scroll layout is per workspace like the others; windows opened under another layout get
a column each, at the end, when it is switched to. The columns are remembered when you
switch away and back. Only the scroll actions need key bindings, none are bound by default
(`config/init.lua` has commented examples).

## Animations

Windows fade in while growing slightly when they open, and fade out while shrinking slightly
when they close (drawn from a copy of their last frame). Tiles glide to their new place when
the layout changes; their new size shows as soon as the application draws it. The scene graph
has no transform for a whole window, so the scaling resizes each of its surfaces about the
window's center. `animations = { enabled = false }` turns this off, and `duration` sets the
length in milliseconds (default 120, 10–1000). Window positions reported by `shaodesk msg` are
always the final ones, and clicks and hovering use them too: input never waits for an
animation or lands in the middle of one.

Animations are interruptible: a tile that is gliding when the layout changes again continues
from where it is drawn, at the speed it has, and never snaps. Each kind (`open`, `close`,
`move`, `workspace`, `fullscreen`, `focus`) can have its own `duration` (0 turns just that
kind off) and easing `curve`: `"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"`,
`"ease-out-quint"`, `"overshoot"`, `"spring"`, or `"bezier(x1, y1, x2, y2)"`. Moves use the
spring by default. `speed` scales every animation (2 is twice as fast), and a frame later
than `late_frame_ms` (default 80) finishes what is running rather than stuttering through it.
Switching workspace slides the old windows out (copies of their last frames) and the new
ones in, fading, toward the side of the higher-numbered workspace; `animations.workspace.distance`
sets the slide as a share of the monitor's width (default 0.08, 0 only fades). Sticky windows
stay put.
When focus moves, a window's opacity and border color fade to their new values (the `focus`
kind) instead of switching.
The shell's own animations (taskbar buttons sliding aside, notification cards, the on-screen
display) follow `enabled` and `speed` too.

## Effects

Small effects that are off, or harmless, until you ask for them. Each has settings in
[docs/config-reference.md](config-reference.md).

**Dim inactive windows.** `windows.dim_inactive = 0.25` lays black over windows without focus,
so the focused one stands out; it works for every application (unlike `inactive_opacity`,
nothing shows through) and fades over `windows.dim_duration` milliseconds (`animations.enabled =
false` or a duration of 0 switches at once). Fullscreen windows are never dimmed, and clicks go
through the black to the window.

**Peek.** Bind `peek` to a key and every window fades to `peek.opacity` (0.12 by default; 0 hides
them) for as long as the key is held, showing the desktop behind them; borders, the panel and
dimming go with it. `peek_toggle` (and `shaodesk msg peek_toggle`) switches it on and off instead.
The fade takes `peek.duration` milliseconds.

**Night light.** `night_light = { enabled = true, night_temperature = 3400 }` turns the screen
warm in the evening and neutral again in the morning, easing over `transition` minutes around
sunset and sunrise. Give the times (`sunrise = "06:30"`, `sunset = "21:00"`) or a `latitude` and
`longitude` and shaodesk works them out for each day. The actions `night_light_toggle`,
`night_light_on`, `night_light_off` and `night_light_auto` (back to the schedule) override it,
also as `shaodesk msg night_light_toggle` and the tile in the shell's Quick Settings, which shows
whether it is on (subscribers to the control socket hear `night-light on|off auto|on|off`);
`shaodesk msg get night_light` prints the temperature in
kelvin, the override (0 schedule, 1 neutral, 2 warm) and whether the schedule is enabled. The
colours go through the output's gamma table, the way `wlsunset` does it, and a client using
`wlr-gamma-control` still works alongside.

**Magnifier.** `zoom_in`, `zoom_out` and `zoom_reset` (bind them, or `shaodesk msg zoom_in`)
magnify the screen by `zoom.step` (1.25) per step up to `zoom.max` (8), easing over
`zoom.duration` milliseconds. The view follows the pointer and keeps it over the point it clicks:
the pointer moving across the screen pans across the whole desktop. With `zoom.scroll_modifier =
"Super"`, Super plus the scroll wheel steps in and out. Only the output the pointer is on is
magnified, and outputs that are rotated stay at 1x. Screenshots and screen recordings of the
output see the magnified picture. `shaodesk msg get zoom` prints the level and its target in
thousandths.

**Hot corners.** Push the pointer into a screen corner and leave it there for
`hot_corners.delay` milliseconds (150) and the corner's request runs, once per visit:

```lua
hot_corners = {
    top_left = "toggle_overview",
    top_right = "spawn foot",
    bottom_left = "peek_toggle",
    bottom_right = "workspace_next",
}
```

A request is written as for `shaodesk msg`: an action with its argument, or `spawn PROGRAM ARGS`.
The corner is `hot_corners.size` pixels (2) square. Nothing runs while a button is held, a drag is
under way, the session is locked, or a fullscreen window covers the output.

## Monitors

Monitors sit side by side, top-aligned. `outputs.order` lists connector names
(such as `DP-3`) left to right; unlisted monitors follow on the right in the order
they appear. The cursor starts on `outputs.primary`, else on the leftmost monitor. By default each monitor runs its preferred resolution at the fastest
refresh rate available for it. `outputs.monitors`, keyed by connector name, overrides
that per monitor:

```lua
outputs = {
    primary = "DP-3",
    monitors = {
        ["DP-3"] = { mode = "2560x1440@200", scale = 1.25, position = { x = 0, y = 0 } },
        ["HDMI-A-1"] = { mode = "2560x1440@144", position = { x = -2048, y = 0 } },
        ["DP-1"] = { enabled = false },
    },
},
```

`mode` is `WIDTHxHEIGHT` or `WIDTHxHEIGHT@HZ`; the closest refresh rate at that
resolution wins. `scale` is fractional, `transform` takes Hyprland's (and Wayland's)
0–7, `enabled = false` turns a monitor off (never the last one), `vrr = true` enables
adaptive sync where the monitor supports it, and `tiling` overrides `layout.tiling` (see
[Tiling](#tiling)). A key such as `["desc:ASUSTek COMPUTER INC
VG27AQ3A"]` matches the start of a monitor's "make model serial" (listed by `shaodesk msg get
outputs`), as Hyprland's `desc:` does; a connector-name key wins over it. Monitors with a
`position` go there, in logical pixels after scaling; the rest follow in a row to their
right. The whole layout then shifts so its top-left corner is 0, 0, because X11 apps
get no input at negative coordinates; windows move with their monitor. `shaodesk msg get
outputs` prints what each monitor ended up with. Reloading
applies changes without restarting.

Monitors can also be changed while running with any wlr-output-management client (`wlr-randr`,
`kanshi`, `wdisplays`): mode, scale, rotation, position, and turning outputs on or off. Those
settings replace the `outputs.monitors` entry of the outputs they touch until the Lua
configuration is reloaded, which brings the configured setup back. The last enabled output
cannot be turned off.

## Input

The `keyboard` table sets the XKB `layout`, `variant`, `model`, `options` and `rules`, and the
`repeat_rate` and `repeat_delay` clients see. An unknown combination is rejected with the rest of
the file, with xkbcommon's reason. `keyboard.file` names an XKB keymap to use instead, absolute,
under `~/`, or relative to the configuration: what `xkbcli compile-keymap --layout us,no >
keymap.xkb` writes, edited to taste, or a hand-written `xkb_keymap { ... }`. A file that cannot
be read or does not compile is an error like any other, shown with the line of the setting and,
where xkbcommon knows it, the line of the keymap (`init.lua:4: keyboard.file:
/home/me/us-custom.xkb:12:5: syntax error`); should it break while the session runs, the
keyboard falls back to the names. Saving an `.xkb` file beside the configuration reloads it, as
saving a `.lua` file there does, so a fixed keymap brings the configuration back.

Pointer devices in a standalone `--session` take `mouse.speed` (-1 to 1),
`mouse.acceleration` (`"flat"` or `"adaptive"`), and `mouse.natural_scroll`; touchpads also
take `touchpad.natural_scroll`, `tap_to_click`, and `disable_while_typing`. Unset settings
keep each device's defaults, and a reload applies changes. Nested sessions get their pointer
from the host, so these do nothing there.

### Keyboard layouts

Several layouts are a comma-separated list, with variants in the same places:

```lua
keyboard = { layout = "us,no", variant = ",nodeadkeys", options = "grp:alt_shift_toggle" },
```

Every keyboard types in the same layout. The `switch_layout` action moves them all to the next
one, wrapping; a binding's `layout = "prev"` goes back instead, and `layout = 2` picks the second
(`shaodesk msg switch_layout`, `switch_layout prev`, `switch_layout 2`; a number past the last
layout is refused). An XKB option such as `grp:alt_shift_toggle` switches from the keyboard
itself, and the other keyboards follow it. Virtual keyboards (wtype, on-screen keyboards) keep
the keymap and layout they bring. Unbound by default:

```lua
{ mods = { "Super", "Alt" }, key = "space", action = "switch_layout" },
```

A reload that changes the keymap gives it to every keyboard at once, keeping the keys held and
the locks such as Caps Lock, and the active layout where the new keymap has it (by name, else
by place); one that leaves the keymap as it was leaves the keyboards alone.
`shaodesk msg get keyboard` prints where the keymap comes from (`source rules`, or `source file
PATH`), a line per layout (`layout`, its number, 1 for the active one, the short name the panel
shows, such as `us` or `no`, and its name), and a line per keyboard (`keyboard`, the layout it
types in, how many its keymap has, 1 for a virtual one, the modifiers it holds and has locked as
bits: Shift 1, Caps Lock 2, Ctrl 4, Alt 8, Num Lock 16, Super 64, and its name), tab-separated.

### Mouse button bindings

Bindings can use a mouse button (`left`, `right`, `middle`, `side`, `extra`, `forward`,
`back`; most mice send `side` and `extra` from their back and forward thumb buttons) in place
of `key`, with or without `mods`. `app_id`, a regular expression, limits one to windows under
the pointer whose app ID matches, and `desktop = true` to the bare desktop; with neither it
acts anywhere. Everywhere else the click reaches the application as usual. Several bindings may
share a button, and the first that matches wins (`action = "none"` hands the click back).
Close, fullscreen, and other window actions act on the window under the pointer, and do nothing
over the desktop. This closes terminals and opens new ones from the thumb buttons, while
browsers keep their own back and forward:

```lua
{ button = "side", app_id = "^(kitty|foot)$", desktop = true, action = "close" },
{ button = "extra", app_id = "^(kitty|foot)$", desktop = true, action = "terminal" },
```

The first window to open within five seconds of a button binding's `spawn` or `terminal` opens centered on
the click, kept inside the area the panels leave free, unless a window rule gives its
`position` (`tests/spawn_at_pointer_smoke.py`). A tile still goes where the layout puts it.

### Nested sessions

The host compositor can consume shortcuts before the nested compositor receives
them: a host that grabs Super (Hyprland, GNOME) keeps these, so set `mod = "Alt"` in
the Lua file for nested sessions. SIGHUP also requests a reload, and
SIGINT/SIGTERM requests shutdown. A reload does not rerun startup commands.

## Appearance profiles

A profile is a named set of look settings, from `appearance`, `windows` and `shell`, that is laid
over the rest of the configuration (and its theme and defaults) while it is in use. Settings a
profile leaves out keep their usual values, and switching profiles changes nothing else:

```lua
profile = "default", -- the one to start with
profiles = {
    default = {},    -- the configuration as it is
    light = {
        appearance = { background = "#dfe4ec" },
        windows = { border_color = "#3d6fd9", border_inactive_color = "#c3cad6" },
        shell = { accent = "#3d6fd9", panel_color = "#f4f6fa", text_color = "#1b2230" },
    },
},
```

Pick one from the panel: the profile button beside the tiling button (three swatches of the
current accent, background and text colours) lists them, the one in use marked, whenever there
are two or more (`shell.widgets.profiles = false` hides it). Right-clicking empty bar space and
pointing at **Appearance** lists them too, beside the menu (the one in use is marked). The command palette (Super + P) lists them as
**Appearance: NAME** too, and scripts and hot corners can use `shaodesk msg profile NAME`, or
`profile next` and `profile prev` to step through them in name order. A pick is saved in
`$XDG_STATE_HOME/shaodesk/profile` (`~/.local/state/shaodesk/profile`) and reloads the
configuration, so the compositor and the shell change together, and it stays after a restart.
While that file names a profile the configuration still has, it wins over `profile`; delete it
to go back to `profile`. Every profile is checked when the configuration loads, so a mistake in
one that is not in use is still reported, with its line. The example configuration ships
`default` and `light`; a file with `extends = "default"` gets them only when it defines no
`profiles` of its own.

## Importing a Hyprland setup

`shaodesk import ~/.config` carries an existing Hyprland/Waybar setup over: monitors, colors
(wallbash or pywal), bar look, gaps, borders, opacity, input, animations on/off, and
wallpaper. It runs
`hyprland.lua` in a sandbox (or parses `hyprland.conf`), writes `theme.lua` beside the
configuration, and reports where each value came from and what it skipped. `init.lua` loads
it with `theme = "theme.lua"` and overrides any of it; `--dry-run` only prints. See
[docs/dotfile-import.md](dotfile-import.md) for the details and for the settings shaodesk
still lacks (rounding, blur, shadows).

## Notifications and on-screen display

The shell is a notification daemon. It owns `org.freedesktop.Notifications` on the session bus
(spec 1.2: `Notify`, `CloseNotification`, `GetCapabilities`, `GetServerInformation`, and the
`NotificationClosed` and `ActionInvoked` signals), so `notify-send`, browsers, chat clients and
`shaodesk`'s own screenshot message reach it without setup. Each notification is a card in the top
right corner of the monitor that has the focus (`notifications.position` moves it to another
corner); further cards stack under it and slide in and out.

- The card shows the application, the summary, the body (the spec's markup: `<b>`, `<i>`, `<u>`,
  `<a href>` for web and mail links, `<br>`; anything else is dropped), the icon (`app_icon`, or
  the `image-path`, `image-data` and `desktop-entry` hints), a progress bar for the `value` hint,
  and a button for each action. A click on the card runs the action named `default`, or dismisses
  the card when there is none; the × dismisses it too.
- `replaces_id` updates a notification in place, and so does a repeated
  `x-canonical-private-synchronous` (or `synchronous`, `x-dunst-stack-tag`) hint from the same
  application, which volume and brightness scripts use. `resident` keeps a notification after
  one of its actions runs, and `transient` keeps it out of the history.
- Cards leave after `notifications.timeout` (6 s), or the application's own timeout. Critical
  notifications stay until dismissed. Hovering a card pauses its timer, which continues with
  the time it had left. At most `notifications.max_visible` cards show; the oldest makes room.
- The clock's flyout keeps the last `notifications.history` notifications, by application (the
  one with the newest first; one with more than two shows its newest two until expanded), each
  with its summary, body, picture, progress, how long ago it came and its actions' buttons. The
  clock counts those not yet seen; opening the flyout marks them seen. Clicking an entry runs its
  default action, the × that shows while the pointer is over it removes it, and Clear all empties
  the list. The flyout opens on the monitor under the pointer with `notification_history` too
  (Super + N), and from a bell on the bar with `shell.widgets.notifications = "bar"`.
- Do-not-disturb (`dnd_toggle`, `dnd_on`, `dnd_off`, `shaodesk msg dnd [on|off|toggle]`, a
  right-click on the clock or the bell, the switch in the flyout, the tile in Quick Settings, or
  `notifications.dnd = true` to start that
  way) keeps cards away and lets everything reach the history; critical notifications still show.
  A reload of the configuration does not undo what was toggled.

The on-screen display is a small pill near the bottom centre of the focused monitor with an icon,
a label and a level, which fades out after `osd.timeout` (1.5 s). It appears when the default
sound output's volume or mute changes, whether from the panel's volume control, a media key bound
to `wpctl` or `pactl`, or another program; when a backlight's brightness changes (a laptop's
keys, `brightnessctl`); when do-not-disturb changes; and for
`shaodesk msg osd TEXT [PERCENT]`, where a last word from 0 to 100 (optionally with `%`) is the
level, e.g. `shaodesk msg osd "Keyboard light" 60`. Set `osd.enabled = false`, or only
`osd.volume`/`osd.brightness` to false, to turn it off.

```lua
notifications = { position = "top-right", timeout = 6000, max_visible = 4, dnd = false },
osd = { position = "bottom", timeout = 1500 },
```

Set `notifications.enabled = false` to run another daemon (mako, dunst): the shell then leaves
the bus name alone, and if another daemon already holds it the shell logs that and serves
nothing (the flyout shows only the calendar). The daemon needs Qt's D-Bus module at build time; without it (or with
`-DSHAODESK_NOTIFICATIONS=OFF`) the shell builds without notifications and the display still works.
Settings are in the [configuration reference](config-reference.md).

The tests start a dbus-daemon of their own on a private address and never touch the session bus:
`notifications_test` (the model, timers and markup), `notifications_dbus_test` (the interface on
that bus), `notifications_smoke` (a headless compositor and shell: cards, a click, expiry, hover,
do-not-disturb, the display and the history), and the configuration tests.

## System tray

The panel shows the status icons applications put in a system tray, on every monitor's bar, in the order they appeared. These are StatusNotifierItems, the kind KDE and Qt
applications, Electron applications and Ayatana's indicator library show; the older X11 tray
icons (XEmbed) are not shown. An icon is the item's named icon, looked up first in the folder the
item names and then in the icon theme, or else the pictures it sends, at the size nearest the
panel's. While the item needs attention it shows its attention icon, and an overlay icon sits in
its bottom right corner. Items that say they are passive stay hidden, and so does the tray when
none is left. Hovering an icon shows its tooltip.

- Left-click activates the application, which usually shows or hides its window. An item that is
  only a menu opens its menu instead, and so does one that cannot be activated, as Ayatana's
  (nm-applet's, for one) cannot.
- Right-click opens the item's menu above the icon (below it on a top panel), in the style of the
  panel's own menus: separators, check marks, radio buttons, icons, greyed-out entries, and
  submenus, which open beside their entries, the menu staying open. The application hears of each
  level as it opens and closes, so one that fills a submenu in when asked can. Clicking an entry
  runs it and closes the menu; so do a click elsewhere, Escape, and another right-click on the
  icon. An item without a menu is asked to show its own.
- Middle-click is the item's secondary action, and the wheel scrolls it a notch at a time, which
  some applications use for the volume or to switch between things.

The shell serves `org.kde.StatusNotifierWatcher`, the registry applications register their items
with, on the session bus, and registers as a tray (`org.kde.StatusNotifierHost-PID`). When another
program serves the watcher already, such as another panel running alongside, the shell shows that
watcher's items instead, and takes the name over if that program quits; applications register
again by themselves. `shell = { widgets = { tray = false } }` hides the tray and gives up both
names, so that applications do not think their icons are shown; a reload follows the setting. The
tray needs Qt's D-Bus module at build time; without it (or with `-DSHAODESK_TRAY=OFF`) the shell
builds without a tray.

The tests use a bus of their own and `tray_probe`, an item with a menu of every kind of entry:
`tray_dbus_test` (the watcher, the host, the icons, menus, and a seeded fuzz of malformed items and
menus), `shell_ui` (clicks, the wheel and the menu in the panel) and `tray_smoke` (a headless
compositor with two monitors and the shell, checked with screenshots).

## Sessions

`shaodesk msg session save NAME` writes what the desktop looks like to
`$XDG_STATE_HOME/shaodesk/sessions/NAME` (`~/.local/state/shaodesk/sessions`): each monitor's
current workspace, tiling on or off and the tiling layouts of its workspaces, and for every
window its monitor, workspace, floating place, tiled, floating, minimized, sticky, maximized,
fullscreen, scratchpad and focus state, the app ID and title to find it by, and the command line
it was started with. `session restore NAME` puts every window that matches back where it was (app
ID and title first, then app ID alone; a window is matched once), switches the monitors to their
saved workspaces and refocuses the window that had focus. Windows of applications that are no
longer running are left alone, unless the request is `session restore NAME launch`, which
starts their saved command lines and places each new window as it opens (the first window with
the app ID, within 30 seconds). `session list` prints `NAME`, its window count and when it
was saved; `session delete NAME` removes one. Names are letters, digits, `.`, `_` and `-`.
Applications that do not restart from their own command line (Flatpak launchers, single-instance
programs that hand over to a running copy) are matched but may not relaunch.

For workspaces that tile with the scrolling layout the session also keeps the width of each
column and which column, and place in its stack, each tile had; a restore puts the windows it
finds back into those columns. A window started by `restore ... launch` arrives later and opens
as usual, right of the focused column.

## Control socket

A control socket runs any Lua action from scripts or other tools:
`shaodesk msg workspace 2`, `shaodesk msg toggle_tiling`, `shaodesk msg spawn foot`,
`shaodesk msg screenshot window`, `shaodesk msg resize_left 80`. Prefixing
`output NAME` makes workspace and tiling actions switch that monitor instead of the focused
one: `shaodesk msg output HDMI-A-1 workspace_next`. The query
`shaodesk msg get workspace` prints the focused monitor's workspace, `shaodesk msg get workspaces`
prints one tab-separated line per monitor (name, current workspace, focused, the
workspaces holding windows, such as `1,3`, or `-`, and tiling, `on` or `off`),
`shaodesk msg get tiling` prints `on` or `off` for the focused monitor, `shaodesk msg get outputs` prints one tab-separated line per monitor (name,
enabled, x, y, logical width and height, scale, transform, mode, and "make model serial"), and
`shaodesk msg get windows` prints one tab-separated line per window:
workspace, focused, minimized, tiled, x, y, width, height, app ID, title, monitor,
visible, scratchpad (a window hidden there is also minimized), sticky, and its window group
(a number; 0 for none). `shaodesk msg get pid_at X Y` prints the process ID of the window
drawn at that layout point, or nothing over bare desktop. `shaodesk msg get layers` prints one line per panel or other layer-shell surface:
namespace, output, layer (0 background to 3 overlay), and whether it is shown.
`shaodesk msg get power` prints one line per power action logind carries out (`poweroff`,
`reboot`, `suspend`, `hibernate`) with its answer: `yes`, `no`, `challenge` (after a password),
`na`, `unknown` until it has answered, or `unavailable` without logind.
`shaodesk msg get animations` prints the number of running animations and of window trees in
the scene (closing windows count until their animation ends), and of focus fades, mainly
for tests. A client
that sends `subscribe` keeps its connection and receives `tiling on|off` and
`workspace N` (the focused monitor's), one `output NAME N USED TILING` line per monitor
(as in `get workspaces`), and `keyboard-layout N COUNT SHORT NAME` (the active
[keyboard layout](#keyboard-layouts), from 1, of how many, as in `get keyboard`) after every
change, plus `launcher OUTPUT` when the `launcher` action
(Super + R) asks the panel on that monitor to open or close its application menu; the panel uses this. The window switcher sends `switcher OUTPUT SELECTED COUNT` followed by
COUNT lines `switcher-window APP_ID TITLE OUTPUT WORKSPACE MINIMIZED URGENT` (tab-separated) when it
opens or a listed window closes, `switcher-select N` as the selection moves (both counting
from 0), and `switcher-close`. The state ends with `power ACTIONS`, the power actions that may
run (`lock,suspend,reboot,poweroff,logout`, in the order menus list them, or `-`), a power
action that fails or is cancelled after it was accepted sends `power-error MESSAGE`, a program
that `spawn` or `terminal` could not start sends `spawn-error MESSAGE` (the panel shows either
across itself for eight seconds), and `power_menu` sends `power-menu OUTPUT`. Children of the session find the socket through `SHAODESK_SOCKET`. Actions are
refused while the session is locked.

## Screen locking and idle

Screen locking uses the standard `ext-session-lock-v1` protocol, so lockers such
as swaylock or gtklock work; the `lock` action starts the one `power.lock_command` names
(`{ "swaylock", "-f" }` unless set; `{}` for none), and is refused while that program is not
installed. The desktop is covered
before the locker draws, only the locker receives input, and if it crashes the
session stays locked until a new locker takes over. Idle notification and idle
inhibition (`ext-idle-notify-v1`, `idle-inhibit-unstable-v1`) let swayidle lock
or blank after inactivity while video players keep the session awake. While a standalone
session is on screen it holds a logind sleep inhibitor, so an idle daemon left running by
another desktop on a different VT cannot suspend the machine; switching VTs away releases it.
This needs sd-bus from libsystemd, libelogind, or basu at build time.

## Power

The power button in the bottom-right corner of the application menu, as in the Windows start
menu, opens a menu above it of what may run now: Lock screen (with a locker installed),
Suspend, Hibernate, Restart, Power off (as logind allows them) and Log out; the button is left
out when nothing may. A click elsewhere in the application menu closes only the power menu. Its entries, and what is refused, come from the
compositor, which also reports across the panel an action that fails or is cancelled later.
Restart, Power off and Log out ask first: a dialog in the middle of the monitor counts down
`power.countdown` seconds (10; 0 waits for a click) and then goes ahead, as do its button and
Enter, while Escape, Cancel or a click beside it gives up. The actions themselves, bound to keys
or sent with `shaodesk msg`, do not ask. The `power_menu` action opens the application menu
with the power menu up on the monitor under the pointer, with the keyboard in it: Up and Down
choose, Enter runs, Escape closes the power menu.

The `suspend`, `hibernate`, `poweroff` and `reboot` actions ask logind (systemd-logind or
elogind, on the system bus) to suspend, hibernate, power off or restart the machine, letting it
ask for a password when its policy wants one. An action logind does not allow on this machine
(`CanHibernate` answering `no` or `na`, say, without swap to hibernate to) is
refused with that reason; one it turns down later, such as a password not given, is reported
across the panel. `shaodesk msg get power` shows what logind allows. This needs sd-bus from
libsystemd, libelogind, or basu at build time. A headless compositor (`--headless`, as the
tests run it) never uses the machine's logind: only one on the bus that `SHAODESK_LOGIN1_BUS`
names. `logout` ends the session, as `quit` does, and needs no logind.

`poweroff`, `reboot` and `logout` first ask every window to close, as its close button would,
so that applications save their state, and go ahead once the last has gone (a log out also
waits for those applications to disconnect, so one saving as it quits is not cut off). A
window still open after `power.close_timeout` milliseconds (5000), typically an application
asking whether to save, cancels the action and stays on screen with the reason across the
panel; answering it in time lets the action go on. `power.force = true` goes ahead after the
timeout anyway, ending such applications without saving, and `power.close_windows = false`
skips the closing.

`suspend` and `hibernate` first lock the screen with `power.lock_command` and ask logind only
once the lock holds on every monitor, so the machine never wakes up unlocked; a locker that has
not locked within five seconds cancels the suspend. A sleep something else asks for (closing
the lid, an idle daemon, `loginctl suspend`) locks the same way: shaodesk holds a logind delay
inhibitor and lets it go once the lock holds, or when logind stops waiting (its
`InhibitDelayMaxSec`, five seconds by default). `power.lock_before_sleep = false` sleeps
without locking, and so does a session with no locker installed; turn it off if an idle daemon
already locks before sleep (swayidle's `before-sleep`), or two lockers race.

```lua
power = {
    lock_command = { "swaylock", "-f", "-c", "000000" },
    close_timeout = 10000, -- ten seconds for applications to close
    countdown = 5,         -- the confirmation's seconds
},
```

## Screenshots and screen sharing

The `screenshot` action (Print, or `shaodesk msg screenshot region|output|window`) runs
[`grim`](https://sr.ht/~emersion/grim/), with [`slurp`](https://github.com/emersion/slurp)
to select a region, and saves `Screenshot_<date>_<time>.png` in `$XDG_PICTURES_DIR/Screenshots`
(from the environment or `user-dirs.dirs`), else `~/Pictures/Screenshots`. The `output` mode
captures the monitor under the pointer and `window` the focused window's area as it appears on
screen, including anything overlapping it. It also copies the image to the clipboard with
`wl-copy` and announces the file with `notify-send`, when they are installed. Lua
`screenshots = { directory = "~/Shots", clipboard = false, notify = false }` changes that; in a
binding, `mode = "output"` picks the mode (default `region`). Without grim (or slurp for a
region), `shaodesk msg screenshot` fails with a message and a key binding logs one.

Screenshots and screen sharing use wlr-screencopy, export-dmabuf, and
ext-image-copy-capture, so `grim` works directly and Discord, OBS, or a browser share a
monitor or a single window through xdg-desktop-portal-wlr. A shared window is drawn on
its own, without whatever overlaps it, and keeps streaming while minimized or on another
workspace.

## Application compatibility

Browsers and Electron applications (Firefox, Chromium, Discord) get the protocols they
look for: GPU buffers through linux-dmabuf with explicit sync where the driver supports it,
viewporter, fractional scaling, presentation timing, xdg-output, middle-click paste
(primary selection), clipboard managers (`wl-clipboard`, data-control), drag-and-drop,
pointer lock and relative motion for games, and xdg-foreign for portal dialogs.
xdg-activation lets an application raise itself, so a link clicked in a chat brings the
browser forward; shaodesk honours every valid token and does not prevent focus stealing.
Popup menus are kept on the output of their window.

Firefox and GTK applications draw their own minimize, maximize, and close buttons, laid
out by GTK's `button-layout` setting, which desktops without title bar buttons (HyDE on
Hyprland, for one) leave empty. shaodesk gives the applications it starts a dconf profile
(`DCONF_PROFILE`) that locks that one setting to `windows.buttons`, by default
`"appmenu:minimize,maximize,close"`; `""` keeps the desktop's value. Every other GTK
setting still comes from, and is saved to, your own dconf database, so other sessions
see no change. This needs `dconf` at startup.

### Portals

Portals run as D-Bus services, started with the bus's environment rather than the
compositor's. A standalone `--session` therefore exports `WAYLAND_DISPLAY`, `DISPLAY`,
`XDG_CURRENT_DESKTOP=shaodesk`, `XDG_SESSION_TYPE`, and `SHAODESK_SOCKET` with
`dbus-update-activation-environment --systemd` before it starts anything; the nested mode
leaves the host's portals alone, so its applications share and pick files through the host.
The installed `shaodesk-portals.conf` selects `xdg-desktop-portal-wlr` for screen sharing and
screenshots and `xdg-desktop-portal-gtk` for everything else. Install both, plus PipeWire
and `slurp` (the wlr portal's monitor picker on multi-monitor setups). An
`xdg-desktop-portal` that is already running keeps the desktop it started with; after
leaving another desktop, run `systemctl --user restart xdg-desktop-portal` once or log out
fully. The session also sets `MOZ_ENABLE_WAYLAND=1`, `ELECTRON_OZONE_PLATFORM_HINT=auto`,
and `_JAVA_AWT_WM_NONREPARENTING=1` unless they are already set.

### XWayland

X11 applications run through XWayland when wlroots is built with X support and
`Xwayland` is installed. `DISPLAY` is set from the start, but Xwayland only
starts when the first X11 client connects and exits again once idle (Lua
`xwayland = false` disables it; changing it needs a restart). X11 windows take
part in focus, the taskbar, snapping, maximize, and fullscreen like Wayland windows.

wlroots' X11 window manager can leave events unprocessed, which loses the
first window after Xwayland starts. Until wlroots fixes this, shaodesk nudges the
window manager every 250 ms while Xwayland runs. With wlroots patched by
`packaging/patches/wlroots-xwm-drain.patch`, configure with
`-DSHAODESK_XWM_WAKER=OFF` to drop the workaround.
