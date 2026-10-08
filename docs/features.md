# Features

What shaodesk does, area by area. The default key bindings are in the [README](../README.md#default-bindings),
and every Lua setting, with its type, default and range, is in the
[configuration reference](config-reference.md).

## Shell

`shaodesk-shell` draws the desktop and, on every monitor, either a menu bar and a dock in
[the macOS style](#the-macos-style), where the shipped configuration starts, or a taskbar with its
[start menu](#start-menu) (`shell.style = "taskbar"`, the profiles `default` and `light`). Both
have [Quick Settings](#quick-settings), the [clock's flyout](#clock-and-calendar) and the
[system tray](#system-tray), and the shell draws the overlays the compositor asks for: the
[window switcher](#window-switcher), the [overview](#overview)'s text, the
[command palette](#command-palette), the [power](#power) dialog, and the
[notification cards and on-screen display](#notifications-and-on-screen-display). The desktop
shows the wallpaper and the configured launchers as shortcuts, which open on a double-click; a
right click on it offers Show desktop and the appearance profiles. Installed applications are read
from desktop entries through GIO, and again as soon as one is installed or removed.

### The macOS style

With `shell.style = "macos"`, as in the shipped `macos-light` and `macos-dark` profiles, the
shell is laid out and drawn as macOS's, in the profile's colours, and the panel settings apply to
the dock.

- **Menu bar**, along the top of every monitor. A spiral at the left opens the system menu:
  Appearance (the profiles), Wallpaper…, Sleep, Restart…, Shut Down…, Lock Screen and Log Out…;
  the `power_menu` action opens it too. The focused application's name, in bold ("Desktop" when
  no window has focus), opens its menu: its desktop actions, New Window, Hide, Hide Others and
  Quit. The **Window** menu acts on the focused window: Minimize, Zoom, Tile Window to Left or
  Right of Screen, Enter Full Screen, Float, Keep on All Workspaces, Move to a workspace or
  monitor, the application's other windows, and Close Window. While one menu is open, pointing at
  another title or pressing Left and Right switches to it. At the right are the widgets
  `shell.widgets` puts on the bar, a search button (the command palette, drawn as Spotlight), a
  Control Center button (Quick Settings) and the date and time, which open Notification Center:
  the notifications as cards with the calendar under them, and the unread count beside the time.
  Applications' own menus (File, Edit, ...) are not shown.
- **Dock**, centred along the bottom: the Launchpad button, the pinned applications, the running
  ones that are not pinned, and the Trash (which opens `trash:///`). A dot sits under each running
  application and its name shows above it on hover. A click starts an application, its icon
  bouncing until a window opens, or brings its windows forward; one asking for attention bounces
  three times. A right click lists its windows and actions, Keep in Dock or Remove from Dock, Hide,
  Quit and Force Quit, which kills its processes as the taskbar's Kill process does. Something dragged from an application and resting on an icon brings its window
  forward, or lists its windows to rest on one, as on the [taskbar](#taskbar), its name showing
  meanwhile. `panel_height` sets its height (the icons are 16 pixels smaller), `panel_radius` its
  corners and `panel_margin.bottom` how far it floats above the edge; it is always at the bottom
  and as wide as its icons, and the desktop beside it takes clicks. Super + B (`taskbar_focus`,
  Control + F3 on macOS) walks its icons from the keyboard as on the [taskbar](#taskbar), the
  Launchpad button and the Trash among them: the icon selected is ringed and named on its tag,
  Enter clicks it, the Menu key opens its menu, and Up goes into a stack's list of windows.
- **Launchpad** (Super + R, the `launcher` action, or the dock's first button): every application
  on pages of a 7 × 5 grid over the whole screen. Type to search, use the arrows, Page Up and
  Page Down, the wheel, dragging or the dots to page, Enter to launch, and right-click for an
  application's actions or Keep in Dock; Escape clears the search and then closes it, as does a
  click beside the icons.
- **Spotlight** is the command palette (Super + P) as a large search field a quarter of the way
  down the screen, its results grouped under headings; **Control Center** is Quick Settings as
  rounded modules, with Display and Sound under their own headings; notifications show as banners
  at the top-right, and Alt + Tab shows large application icons in a row.
- **Menus** have dense rows, the highlighted one filled with the accent colour.
- **Wallpaper**: without `shell.wallpaper`, the shell draws an abstract one of soft hills in the
  accent's hues over a sky in `appearance.background`'s, light or dark with the profile.

The shipped macOS profiles also give windows macOS's controls and shadows
(`windows.controls = "traffic_lights"`, `round = "always"`, `shadow = { enabled = true }`, see
[Decorations](#decorations-focus-and-fullscreen)) and put GTK's close, minimize and maximize on
the left (`windows.buttons`, read as shaodesk starts). They name the Inter font, the closest free
match for macOS's; without it the default sans-serif stands in. Switching to or from a macOS
profile re-lays the shell out at once.

### Taskbar

Along the bar, from the left: the start button, the pinned applications and a button for each
open window, the workspace indicator, the widgets placed on the bar, the tray, the Quick Settings
button, the clock, and a sliver at the end that shows the desktop. A button shows a tooltip when
the pointer rests on it, but for a window's, which shows pictures of its windows instead
([below](#window-pictures)). Lua configures the panel's height, top or bottom placement
(`panel_position`), margins that make it float (`panel_margin`, one number or
`{ top, right, bottom, left }`), corner radius, font and text size, colors (`#RRGGBB`, or
`#RRGGBBAA` for a translucent panel), wallpaper, and pinned commands, which run from your home
directory.

Clicking a window's button focuses it, or minimizes it when it is focused already, as on Windows,
from any monitor's bar; a middle click closes the window, a right click opens its
[menu](#taskbar-menus), and dragging the button moves it along the bar. An application's windows
share one button, stacked with a count when there are several: clicking it cycles through them,
and resting on it shows them to pick one, close it with its cross or a middle click, or open its
menu (`group_windows = false` gives every window its own button). A line under a button marks its
window: long in the accent colour for the focused one, short for the others, dimmed while
minimized; a window asking for attention tints its button in the urgent colour. Installed
applications can be pinned to the taskbar from a window's menu or the start menu, and unpinned
from their button's menu; those pins are kept in `$XDG_STATE_HOME/shaodesk/pinned`
(`~/.local/state/shaodesk/pinned`), one desktop id per line, and stay off the desktop. A pinned
application's windows take its button's place, keeping its icon, and dragging the button moves the
pin.

Something dragged from an application (a file, text, a link) brings windows forward as on Windows:
resting the drag on a window's button for half a second brings that window to the front, restored
if it was minimized and its workspace shown, so that the drag goes on onto it and drops there.
Resting on a stack's button shows its windows, as the pointer resting there does, and resting on
one of them brings it forward; they stay while the drag is over them or the button, and go once it
has left both or ended. The button, picture or title under the drag is lit as under the pointer.
A drag only crossing the bar brings nothing forward, and nothing can be dropped on the bar or on
the windows it shows: the application sees the drop cancelled, so a file moved that way is not
lost, and a pinned application's button without windows does nothing with it. Once the drag ends,
the window brought forward has the keyboard.

The bar works from the keyboard too, as Windows' Win + T has it: Super + B (the `taskbar_focus`
action, or `shaodesk msg taskbar_focus`; Super + T arranges windows here) gives the keyboard to
the bar on the focused monitor, with the button of the window that had it selected, else the
first. A ring in the accent colour marks the selection. Left and Right, Home and End move along
the buttons of windows and of pinned applications (the start button and the widgets are left out),
and a button with windows shows them at once, as resting the pointer on it does, the card gliding
from one button to the next; a pinned application's button shows its name. Enter or Space
does what a click does and gives the keyboard back: it brings the window up (restored, its
workspace shown), brings up a stack's next window, starts a pinned application, or minimizes the
window that had the keyboard, which counts as the focused one. Up (Down on a bar along the top)
goes into the card of [pictures](#window-pictures) or a stack's list, and Down (Up) back to the
button. The Menu key or Shift + F10 opens the button's [menu](#taskbar-menus), or the menu of the
window selected on the card, with its first entry highlighted, and closing it comes back to the
bar. Escape, or Super + B again, gives the keyboard back to the window that had it and changes
nothing. Moving the pointer over the bar or the card, or a press anywhere, hands the bar back to
the pointer: what it is on shows as on hover, a click on the bar or the card goes on to what it
lands on, and one elsewhere only gives the keyboard back, as beside a menu. While the bar has the
keyboard no window has it, so no button is marked as focused. A screen reader names the selected
button or window as the selection moves.

The workspace indicator lists the monitor's workspaces, the current one on a pill, with a dot
under those with windows: click one to switch to it, or scroll anywhere on the bar to page through
them. A right click on the bar's empty space opens its menu: tiling on or off, the start menu,
show desktop, and the appearance profiles beside their entry.

The bar moves with what happens on it, briefly and without holding anything up: a button's fill
fades in under the pointer and out after it, an application's icon shrinks a little while it is
pressed, a window's button fades and grows in as the window opens and shrinks away as it closes
while the others slide over, an application just pinned grows into a slot that opens for it, the
line under a button eases to its new length and colour, the workspace pill slides to the workspace
shown, a count grows in and pops as it goes up, and an icon that follows a state (the network, the
volume, do-not-disturb, tiling) crossfades as the state changes.

### Window pictures

Resting the pointer on a window's button, or a stack's, opens a card above it (below a bar along
the top) as on Windows 11: a small picture of each window side by side, under the application's
icon and the window's title. The pictures follow the windows as they redraw while the card is
open (30 times a second at most, so a video moves), show the window alone, without its frame or
what covers it,
and show a minimized window or one on another workspace too; until one has come, the
application's icon stands in, and the first fades in over it. They are taken from halfway into
the delay, so that the card opens on them, and none is taken for a button the pointer only
crosses on its way. Alt + Tab shows the same pictures, a card for each window in the
[window switcher](#window-switcher). Clicking a picture
focuses its window, or minimizes it when it is focused already; a middle click or the cross shown
over the one under the pointer closes it, and a right click opens its [menu](#taskbar-menus). The
focused window's picture is marked as its button is. The pointer can cross from the button onto
the card, which closes a moment after it has left both, or at once when a button is pressed or a
menu opens; moving onto another button shows that one's windows at once, the card gliding over
to it and easing to their width, as it eases to a new width when a window opens or closes. A
drag resting on a stack's button opens the card too, and resting on a picture brings its window
forward ([above](#taskbar)). From the keyboard on the bar ([above](#taskbar)), Left and Right
move between the pictures, the selected one ringed and showing its cross; Delete closes its
window, as the cross does, and Enter brings it up. The pictures get narrower when more windows
than fit across the monitor share a button, down to 60 % of their width, and past that the stack
lists its windows by title instead. The macOS style's
dock lists a stack's windows by title, pictures or not.

While a window's process plays sound, a speaker shows beside its title on the card, as on a
browser's tab; clicking it mutes the window's sound, and the speaker stays, crossed out, while it
is muted, to unmute it. The sound is that of the sound server's streams (PulseAudio's, or
PipeWire's) played by the process that made the window, or by one it started that has no window
of its own: a browser playing from a child process shows it, and so does a terminal running a
player, unless the player has a window. Windows of one process cannot be told apart, so all of a
browser's windows show the speaker when one of its tabs plays, as do all of a terminal's that
serves every window from one process (foot's server, GNOME Terminal), and muting one mutes them
all. A stream counts while it is open and not paused: an application that keeps one open while it
plays nothing, as some games and voice-chat applications do, shows the speaker until it closes
it. An application in a sandbox with a process namespace of its own (Flatpak) names its processes
by numbers that mean nothing outside it, so its windows show no speaker.

Resting the pointer on a picture for half a second peeks at its window, as Windows' Aero Peek
does: every other window fades as for the desktop [peek](#effects), to `peek.opacity` over
`peek.duration`, and the window shows over them in full where it is, a minimized one where it
was and one on another workspace over the workspace shown. The bar and the card stay as they are.
Nothing about the window changes: it is not focused, raised, restored or moved to the workspace
shown, and while it shows only for the peek, clicks go through it. Moving onto another picture
takes the peek over at once, without the others coming back in between; leaving the pictures, or
the card closing, ends it with a fade. A picture the keyboard on the bar selects peeks the same
way, half a second after the selection comes to it, and at once while another is peeked at.
Clicking the picture focuses the window where the peek shows it. The peek also ends as the window closes or is focused another way, as the session
locks, or as the desktop peek starts. The macOS style's dock and a stack's list of titles have no
pictures, and do not peek.

```lua
shell = {
    thumbnails = {
        enabled = true, -- false: a tooltip with the title, and a stack's list of titles
        delay = 400,    -- milliseconds the pointer rests on the button first (0 to 2000)
        size = 240,     -- width of one picture in pixels (120 to 480), 5/8 of it tall
        live = true,    -- false takes one picture of each window just before the card opens
    },
}
```

### Clock and calendar

Beside the time, the clock shows how many notifications have not been seen, muted (with a
crossed-out bell) while do-not-disturb is on, which a right click on it toggles. Clicking the clock
opens its flyout at the bar's right end, as on Windows 11: the
[notifications](#notifications-and-on-screen-display) on one card, and under it a calendar with
the time and today's date over the month, today marked and the weeks starting on the locale's
first day. The arrows or the mouse wheel page through the months, each sliding in; the title zooms
out to the year's months and then to a decade's years, where a pick zooms back in; Today returns
to this month. On a monitor too short for both cards, the calendar leaves out the time and the
date for the notifications, whose list scrolls in what room is left, fading out at the edge where
it goes on.

### Quick Settings

Left of the clock, as on Windows 11, the Quick Settings button shows the volume and the battery in
small icons, and the network only while its link is down, dimmed and struck through as a warning
(a link that is up shows only in Quick Settings); its wheel changes the volume and a middle click
mutes. Clicking it opens Quick Settings at the bar's right end: what is playing
([media controls](#media-controls)), tiles for [Wi-Fi](#wi-fi) where NetworkManager has a Wi-Fi
device, [Bluetooth](#bluetooth) while BlueZ has an adapter, do-not-disturb, night light (on or off
against its schedule, through the compositor), tiling on this monitor (greyed out where tiling is
not available), the [power mode](#power-mode) while power-profiles-daemon runs, the appearance
profile (listing the profiles under it) and the wallpaper (opening the same picker as the bar's
button), and without NetworkManager's Wi-Fi the network's state on a disc rather than a tile, as it
is only shown (Wi-Fi, wired or down, and the interface, or NetworkManager's name for the
connection); a brightness slider where the screen has a backlight
(set through logind's `SetBrightness`, so no privileges are needed); the volume with its mute, the
outputs to play through and each application's volume a click away; and the battery's charge
along the foot.

### Media controls

What is playing shows at the top of Quick Settings, as on Windows 11 (Now Playing in the macOS
style's Control Center): the cover, the title and the artists, the player's icon and name, where
the track is and how long it is, and previous, play or pause and next, greyed out where the player
does not offer them. A press or a drag on the position seeks, where the player can; a click on the
player's name brings its window forward. With several players, arrows beside the name step
through them, and the one picked stays until another starts playing. The card is there only
while a player is (`shell.widgets.media = false` leaves it out), and the Quick Settings button
stays on the bar for it even when every other widget is placed there. The position moves on
while the flyout is open; closed, nothing is read or redrawn.

The media keys (Play/Pause, Next, Previous and Stop, bound to the actions `media_play_pause`,
`media_next`, `media_previous` and `media_stop`, which `shaodesk msg` runs too) control the same
player, as on Windows 11 and KDE. The shell finds players through MPRIS
(`org.mpris.MediaPlayer2.*` on the session bus), which music and video players offer, and
browsers for a tab playing sound or video. The players playing come first, the one that started
last ahead; then those that played, by when they stopped; then those already paused or stopped
when the shell found them. A player stopped with nothing loaded is left out. The keys do nothing
while there is no player, and the shell needs Qt's D-Bus module at build time for any
(`-DSHAODESK_MEDIA=OFF` builds without them).

### Power mode

While power-profiles-daemon runs, Quick Settings has a Power mode tile, as GNOME and KDE have: it
shows the profile in use (a leaf for Power saver, a gauge for Balanced, a bolt for Performance), lit
while that is not Balanced, and a click lists the profiles the daemon offers to pick from. Where
the daemon holds performance back (a laptop on a lap, or too hot), Performance says so. The shell
asks the daemon on the system bus (`org.freedesktop.UPower.PowerProfiles`, or
`net.hadess.PowerProfiles` before version 0.20), which lets the active session's user switch
without a password; a switch the daemon refuses is said across the panel and the tile shows the
profile in use again. Without the daemon the tile is not there, and `shell.widgets.power_mode =
false` leaves it out; `-DSHAODESK_POWER_PROFILES=OFF` builds without it.

### Wi-Fi

Where NetworkManager runs and has a Wi-Fi device, the shell manages Wi-Fi as Windows 11 and KDE do,
through NetworkManager's D-Bus API on the system bus. In Quick Settings (with
`shell.widgets.network = "quick"`, the default) the Wi-Fi tile takes the network state's place:
a click turns the radio on or off, and its chevron lists the networks in range under it. With the
network on the bar (`"bar"`, as the macOS profiles have it), the network widget opens the same list
under a switch for the radio, as macOS's Wi-Fi menu. The list shows one entry for each network
name at its strongest access point: its signal in bars, a lock when it is secured, and whether it
is connected; the one connected comes first, the one being connected to next, the others by
signal, and the devices look for networks again as it opens. A click on a network opens it:

- one NetworkManager knows, or an open one (Enhanced Open too), connects with Connect;
- one secured with a key (WPA or WPA2 Personal, or WPA3 Personal) asks for its password first,
  Connect waiting for the eight characters WPA takes; NetworkManager adds a connection for it
  (`AddAndActivateConnection`) and keeps the password for the next time. A password it refuses
  is asked for again, the connection added for it removed, and what went wrong said across the
  panel;
- the one connected disconnects with Disconnect, and NetworkManager does not connect again by
  itself until asked;
- one that needs an enterprise sign-in (802.1X) or WEP says so, to be set up with NetworkManager's
  own tools.

NetworkManager lets the active session's user do all of this without a password, as its polkit
rules have it. A switch on the machine that holds the radio off (rfkill) greys the tile out. Hidden
networks are not listed. Without NetworkManager everything stays as described above, read from
`/sys/class/net`; `-DSHAODESK_NETWORKMANAGER=OFF` builds without it.

### Bluetooth

While BlueZ runs with an adapter, Quick Settings has a Bluetooth tile, as Windows 11 and GNOME
have: a click turns the adapter on or off, and its chevron lists the paired devices under it, the
connected ones first, each with its kind (headphones, a speaker, a keyboard, a mouse, a phone, a
game controller, a computer) and, where BlueZ reports it, its battery's charge. The tile names the
device connected, or how many are. A click opens a device: Connect or Disconnect, and Forget, which
removes the pairing. Pair a new device looks for the devices in range while the list is open, the
strongest first and only those that give a name; a click on one and Pair pairs with it, then trusts
it (so that it connects by itself from then on) and connects. What BlueZ asks while a device pairs
shows over the list: a passkey to compare with the one the device shows, a PIN or a passkey to
type, a code to type on a keyboard, or whether a device may pair; Cancel says no, and a pairing the
user refused is not reported as a failure. Pairing "just works" where the device asks nothing.

The shell answers BlueZ as an agent of its own (`org.bluez.Agent1`, with the KeyboardDisplay
capability), registered as the default agent the first time the user looks for devices or pairs,
so that a shell never asked takes no part in pairing; a device that asks to pair by itself before
then is handled by another agent, or refused. A connection or pairing that fails is said across the
panel. Without BlueZ the tile is not there, and `shell.widgets.bluetooth = false` leaves it out;
`-DSHAODESK_BLUETOOTH=OFF` builds without it.

### Where the widgets go

Each widget is switched off from Lua: `shell = { widgets = { battery = false, calendar = false } }`,
with `workspaces`, `battery`, `network`, `volume`, `clock`, `calendar` (the clock stays, the
calendar goes), `tiling`, `profiles` (the appearance profile picker), `wallpapers`,
`notifications` (do-not-disturb), `keyboard_layout`, `power` (the [power button](#power) in the
start menu), `tray` (the [system tray](#system-tray)), `media` (the card of what is playing, in
Quick Settings), `power_mode` (its [power mode](#power-mode) tile) and `bluetooth` (its
[Bluetooth](#bluetooth) tile) all on by default. Those that can move sit
on the bar with `"bar"` or in Quick Settings with `"quick"`, and `true` leaves them in their default
place: `network`, `battery`, `volume`, `profiles` and `notifications` are in Quick Settings and
`tiling` and `wallpapers` on the bar. Placed on the bar, each has its button there
(`notifications = "bar"` is a bell with the unread count, which the clock otherwise stands for),
and the Quick Settings button goes once nothing is placed in it:

```lua
shell = { widgets = { volume = "bar", network = "bar", battery = "bar",
                      profiles = "bar", notifications = "bar", wallpapers = "quick" } },
```

On the bar, the volume shows the default output's level (its wheel changes it, a middle click
mutes, a click lists each application's volume and a right click the outputs); the battery shows
the charge of the first battery in `/sys/class/power_supply` (red when nearly empty,
accent-coloured while charging) and is left out on machines without one; the network shows Wi-Fi,
a wired link, or a dimmed struck-through icon when the interface is down, read from
`/sys/class/net` (physical interfaces only, wired preferred), and is left out without any
interface. Both follow the kernel's notices of changes as they come. Where NetworkManager runs,
the network shows the connection the machine goes out through instead, its bars following the
Wi-Fi signal and its tip naming the network, and a click lists the [Wi-Fi](#wi-fi) networks. With
two or more
[keyboard layouts](#keyboard-layouts), the active one's short name (`us`, `no`) sits beside the
clock, and clicking it switches every keyboard to the next.

### Look and feel

Everything takes the panel's `accent`, `panel_color`, `text_color`, `font` and `font_size`:
hovering, pressing and what is open or on are shown by laying the text color over the panel at a
low opacity, which shows on a light panel as on a dark one. Popups (the start menu, menus, the
mixer, the flyouts and the rest) are drawn in the panel color made opaque, even when the bar is
translucent: nothing is blurred behind them, and a window showing through would make them hard to
read. They open in a surface of their own over the whole monitor, above fullscreen windows too, so
the start menu Super + R opens shows over a video; they fade in with a short slide from the bar and
fade out, and cast a soft shadow when the shell draws through the GPU. While one is open, the
keyboard is in it and a press anywhere but on the bar or the popup closes it without reaching what
is under it; a press on another bar button opens that one's popup at once. The overview, the
window switcher and the command palette close it.

Menus (the bar's, a window's, the tray's, the power menu, the desktop's) take the keyboard: Up and
Down (wrapping), Home and End move, Enter or Space chooses, Right opens a submenu and Left or
Escape closes it, and Escape closes the menu; resting the pointer on an entry with a submenu opens
it beside the entry, the menu staying open, and moving the pointer towards an open submenu across
other entries keeps it open. Buttons, search fields, crosses and empty lists look the same
wherever they are, and a ring in the accent colour shows the button the keyboard is at.

The overlays (the switcher, the palette, the power dialog and the overview) come in each time they
show and fade out quicker as they close: the pointer and the keyboard go to what is under them at
once, so nothing waits for the fade. All the shell's motion is short and eases out; it follows
`animations.enabled` (off makes every change instant) and `animations.speed`, and nothing moves,
or wakes the shell, while nothing changes.

The shell draws through the GPU; `renderer = "software"` draws on the CPU instead, for a weak
machine: it starts faster and uses less memory, but cannot draw effects such as shadows. In a
nested session, applications that reuse an existing process or D-Bus service can open in the host
session instead.

### Application icons

The shell finds the application a window belongs to by its app id, so that its button, dock icon,
switcher entry and menu show the application's icon and name. Most windows give their desktop
file's name, but many spell it otherwise, and the shell tries each way from the most exact down:
the desktop file's name in any case, its `StartupWMClass` (as X11 programs, Electron apps and Wine
give it), a Steam game's number (`steam_app_570` for the game's shortcut), the last part of a
reverse-DNS name (`nautilus` for `org.gnome.Nautilus`), the name without what programs add to it
(a version as in `gimp-2.10`, `-bin`, `-desktop`, `-browser`, an AppImage's architecture, NixOS's
`.name-wrapped`), the program the entry runs (`gnome-calculator`, `obs`), the vendor's domain the
entry lacks (`org.xfce.Thunar` for `thunar.desktop`), and the entry's name; a few programs whose
windows name nothing like their entries (`steamwebhelper`, `soffice`) are known by name. A window
whose program has an entry the menus leave out (a password prompt's, a portal's file dialog's)
gets that entry's icon, and one with no entry an icon the theme has by a name guessed the same
ways (`steam_icon_570`), else a generic one.

Icons come from the icon theme the desktop names: Qt's platform theme's (qt6ct, KDE), else GNOME's
`icon-theme` setting where it is set (GNOME Settings, `gsettings`, nwg-look), else
`gtk-icon-theme-name` in GTK's `settings.ini`, else KDE's `kdeglobals`. After it the shell looks
in Breeze, Adwaita or Papirus, whichever is installed, so that the generic icons applications and
the shell name are found under any theme. A change of theme shows after the shell restarts.

### Taskbar menus

Right-clicking a window's button opens its menu, headed by the application's icon and name (from
its desktop entry; the window's app id without one) over the window's title. Then come:

- the application's desktop actions (Firefox's "New Private Window"), with their icons, and
  **New window**, which starts the application again (left out when one of its own actions is a
  new window);
- **Minimize**, or **Restore** for a minimized window, which is all a minimized one offers of
  these three; **Maximize**, or **Restore** for a maximized one; **Fullscreen**, checked while it
  is;
- **Move to workspace**, beside it the workspaces of its monitor (named as
  `layout.workspace_names` names them), the one it is on marked; **Move to monitor**, with two or
  more monitors, from left to right; **Keep on all workspaces**, making it
  [sticky](#sticky-windows) (not with `features = { sticky = false }`); and where its workspace
  tiles, **Float**, checked while it is kept out of the tiling;
- **Pin to taskbar** or **Unpin from taskbar** for an installed application, **Close window**
  in the danger colour, and last **Kill process**, which ends the process that made the window
  with `SIGKILL`, for one that does not close (only once its process is known; never the shell's
  or the compositor's).

None of these focuses the window or raises it: a focused window moved off the workspace on screen
passes the focus on, as `move_to_workspace` does. The window menu names the window itself, never
by its title, so it acts on the right one when several share a title.

A stacked button's menu is about all its windows: its title counts them, and it offers
**Minimize all** (or **Restore all**), moving them all to a workspace or monitor (their own
marked when they share it), **Close all 2 windows**, and **Kill process** (or **Kill 2
processes** when they come from more than one). Right-clicking a window in its hover
list opens that window's own menu. A pinned application's button without windows offers its
desktop actions, **Open** and **Unpin from taskbar**; the bar's own menu has icons too.

### Start menu

The bar's first button (or Super + R) opens the start menu, after Windows 11's, above the button.
Its search field is on top and has the keyboard, so typing searches at once; along the bottom are
the user's name and picture and the [power button](#power). Under the search field are the
applications pinned to the start menu, six to a row on pages of up to three rows (the wheel or the
dots beside them turn the pages; dragging a tile moves it), and under them Recent: the
applications launched lately, with how long ago ("5 min ago", "Yesterday"). All apps › lists every
application from A to Z under its letter, and clicking a letter shows them all to jump to one.

The search finds applications by name, generic name ("Web Browser"), keywords, desktop id and
comment, and open windows, workspaces and actions as the [command palette](#command-palette) does
(its `>`, `@` and `#` too), grouped under Best match, Apps, Open windows and Actions. The best
match is on a card of its own, with an application's desktop actions ("New Private Window") as
buttons beside Open. Among equal matches, what is launched more often comes first; what matches
far worse than the best match is left out. A search that finds nothing says so, with the prefixes
that narrow one.

The arrows, Tab and Page Up and Page Down move through the tiles, the lists and the results, and
Enter opens what the keyboard is at (the best match, as the search starts); at the best match,
Right and Tab move on to its buttons (Open, then its desktop actions) and Left back, Enter
pressing the one ringed. Escape clears the search, then closes the menu. Right-clicking an
application, or the menu key, gives its menu: Open, its desktop actions, Pin to Start or Unpin
from Start, a tile's Move to front, and Pin to taskbar or Unpin from taskbar.

The start menu's pins are its own, as on Windows, kept in `$XDG_STATE_HOME/shaodesk/start-pinned`,
a desktop id per line. Until they are first changed they are the taskbar's pins, then common
applications up to six: the default web browser, file manager, a terminal, text editor, mail
client, image viewer and media player. Every launch of an installed application from the shell
(the start menu, the taskbar, the palette, desktop actions) is counted in
`$XDG_STATE_HOME/shaodesk/launches`: its desktop id, how many times and when last, the 200 most
recent kept. Applications installed or removed show at once, without a refresh. The picture is
`~/.face` or `~/.face.icon`, else AccountsService's when the shell is built with D-Bus, else the
name's first letter on the accent colour; the name is the full name from the password database,
else the login name.

## Windows

### Decorations, focus and fullscreen

Windows that leave decorations to the window manager (Wayland applications that support
server-side decorations, such as kitty, and X11 applications such as Spotify) get no title
bar. Instead, a strip of three flat buttons sits over their top-right corner (minimize,
fullscreen, and close, from left to right) and appears when the pointer nears that corner, so
it never covers text. With `windows.controls = "traffic_lights"` they are macOS's red, yellow and
green circles (close, minimize, fullscreen) at the top-left instead, grey while the window has
no focus and showing their symbols while the pointer is on one. Dragging the window's top edge (its top 6 pixels, as a title bar would;
`windows.drag_strip` changes how many) moves the window; a file or text dragged from another
window over it goes to the window. Other windows, and windows that ask to draw their own frame, decorate
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
(at the output's edges) set them separately, and a tile alone on its workspace has none unless
`layout.smart_gaps = false` (see [Tiling](#tiling)). Hyprland's `gaps_in` is half of `gap_inner`,
since Hyprland adds it on both sides. The `windows` table draws a border around each window
(`border_width`, `border_color` for the focused one, `border_inactive_color`) and sets
`opacity` and `inactive_opacity`, per application too with `rules`. Fullscreen windows have no
border and stay opaque. Windows on a monitor with tiling on (floating ones too) and their
border get rounded corners (`corner_radius`, 10 by default, 0 for square ones) when wlroots is
built with `packaging/patches/wlroots-rounded-corners.patch`; stock wlroots keeps them square.
`round = "always"` rounds every window, on any monitor, but for one that draws a shadow of its
own around it (a GTK frame), which keeps its own corners; fullscreen and maximized windows stay
square.

`windows.shadow = { enabled = true }` draws a soft shadow under each window that has none of its
own, following its corners: `color` under the focused window, the lighter `inactive_color`
under the others, fading out over `blur` pixels and falling `offset` pixels down (or
`{ x, y }`). Fullscreen and maximized windows have none, and a shadow takes no input and is no
part of the window's size, so snapping, tiling and the magnet ignore it. The shadow is cut from
one blurred image, so resizing a window costs nothing. Blur is not available.

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
frames its thumbnail and colours its workspace, the window switcher puts a dot on its icon and
tints its cell, and the command palette lists it first with "needs attention" and a dot on its
icon. `focus_urgent` (Super + U, `shaodesk msg focus_urgent`) focuses the window that
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
list), `switcher_cancel`, Return, or Escape. A minimized window's icon is faded and marked with a
dash, and the caption under the grid gives the selected window's full title, its workspace (by
name too) and monitor, and whether it is minimized or asking for attention. `cycle` is the older action that raises the
least recently focused window on the spot.

With [window pictures](#window-pictures) (`shell.thumbnails`, on by default) the taskbar style
shows each window as a card instead, as Windows 11 does: the application's icon and the window's
title over a picture of the window. The pictures are as tall as the taskbar's
(`shell.thumbnails.size`, 5/8 of it) and each as wide as its window's proportions make it, from
3:4 to 2:1, in rows that wrap and are centred; with many windows they get smaller, down to 60 % of
that height, and past that the rows scroll to the selection. The selected card is framed in the
accent colour, one asking for attention is tinted with a dot on its picture, and a minimized
window's picture is faded and marked with a dash. The pictures follow their windows as they
redraw (`shell.thumbnails.live`), minimized ones and those on other workspaces and monitors too;
the shell asks for them as the switcher opens, before it shows, so that they are there by then,
and until one has come the application's icon stands in, the first fading in over it. The
caption, the keys, the mouse and the moment it shows are as with the grid. The macOS style keeps its large icons, as macOS switches
applications.

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
windows, `#` workspaces and `%` sessions; when nothing matches, the palette says so and lists
these. The palette needs the shell (`shaodesk-shell`), which
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

A tile alone on its workspace has no gaps: it fills the space the panels leave, inside its
border. A second tile brings `gap_inner` and `gap_outer` back for both, and they go again when it
closes, floats or leaves (`layout.smart_gaps`, on by default, like sway's `smart_gaps`; `false`
keeps the gaps around a lone tile too). A window group is one tile; floating, sticky and
maximized windows are not tiles, and two windows in monocle keep their gaps. A lone column of the
scroll layout loses its gaps and keeps its width.

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
The shell's own motion (the taskbar's buttons coming, going and sliding aside, its fills, lines,
counts and icons, the popups, the notification cards, the on-screen display, and the switcher,
the palette, the power dialog and the overview coming in and going) follows `enabled` and `speed`
too.

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
The fade takes `peek.duration` milliseconds. Resting on a window's picture on the taskbar's card
peeks at that window the same way, the others fading while it shows over them (see
[Window pictures](#window-pictures)); the desktop peek takes over from it.

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
`macos-light`, where it starts, and `macos-dark` ([the macOS style](#the-macos-style)), and the
taskbar's `default` and `light`; a file with `extends = "default"` gets them, and starts on
`macos-light` unless it sets `profile`, only when it defines no `profiles` of its own. A profile
wins over a theme.lua that `shaodesk import` wrote, so pick `default` to see that.

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

- The card is headed by the application's icon and name (from the `desktop-entry` hint or the
  application's name, else `app_icon`) and how long ago it came. Under that come the summary, the
  body (the spec's markup: `<b>`, `<i>`, `<u>`, `<a href>` for web and mail links, `<br>`; anything
  else is dropped) and a progress bar for the `value` hint, beside the picture (the `image-path`
  or `image-data` hint, else `app_icon` when the application's own icon heads the card), and a
  button for each action. A click on the card runs the action named `default`, or dismisses the
  card when there is none; the × that shows while the pointer is on the card dismisses it too.
  A critical card is edged in red.
- `replaces_id` updates a notification in place, and so does a repeated
  `x-canonical-private-synchronous` (or `synchronous`, `x-dunst-stack-tag`) hint from the same
  application, which volume and brightness scripts use. `resident` keeps a notification after
  one of its actions runs, and `transient` keeps it out of the history.
- Cards leave after `notifications.timeout` (6 s), or the application's own timeout, which a
  line along the card's bottom edge runs down (with animations on). Critical notifications stay
  until dismissed. The pointer anywhere on a card, its buttons too, pauses its timer and the
  line, which continue with the time it had left. At most `notifications.max_visible` cards show; the oldest makes room.
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
namespace, output, layer (0 background to 3 overlay), whether it is shown, and whether it holds
the keyboard.
`shaodesk msg get power` prints one line per power action logind carries out (`poweroff`,
`reboot`, `suspend`, `hibernate`) with its answer: `yes`, `no`, `challenge` (after a password),
`na`, `unknown` until it has answered, or `unavailable` without logind.
`shaodesk msg get animations` prints the number of running animations and of window trees in
the scene (closing windows count until their animation ends), and of focus fades, mainly
for tests. `shaodesk msg get pictures` prints one line per capture source a client asked for to
picture a window at a size, as the taskbar does: the size asked for, the frame's size, how many
sessions capture it, and the window's title, also mainly for tests. `shaodesk msg get
window_peek` prints one line per window for the taskbar's peek at a window: whether it is the
one peeked at, whether it is drawn, where it is stacked among the windows (0 at the bottom), how
far it shows through the peek and its opacity (both in thousandths), and its title. `shaodesk
msg get seat` prints what has the keyboard, what the pointer is on and, while something is
dragged, what the drag is over: a line each, `keyboard`, `pointer` or `drag`, followed by
`window` and its title, `layer` and its namespace, `other` (a popup, the lock screen) and `-`,
or `-` twice for nothing; during a drag the pointer is on nothing. A client
that sends `subscribe` keeps its connection and receives `tiling on|off` and
`workspace N` (the focused monitor's), one `output NAME N USED TILING` line per monitor
(as in `get workspaces`), and `keyboard-layout N COUNT SHORT NAME` (the active
[keyboard layout](#keyboard-layouts), from 1, of how many, as in `get keyboard`) after every
change, plus `launcher OUTPUT` when the `launcher` action
(Super + R) asks the panel on that monitor to open or close its start menu; the panel uses this. The window switcher sends `switcher OUTPUT SELECTED COUNT` followed by
COUNT lines `switcher-window APP_ID TITLE OUTPUT WORKSPACE MINIMIZED URGENT ID` (tab-separated) when it
opens or a listed window closes, `switcher-select N` as the selection moves (both counting
from 0), and `switcher-close`. ID is the window's number, which the compositor gives each window as
it appears and never gives another; a taskbar gets the same number for its handle of the window
from `shaodesk-window-control-v1` (version 4), and so finds the window the line means where two
share a title. The state ends with `power ACTIONS`, the power actions that may
run (`lock,suspend,reboot,poweroff,logout`, in the order menus list them, or `-`), a power
action that fails or is cancelled after it was accepted sends `power-error MESSAGE`, a program
that `spawn` or `terminal` could not start sends `spawn-error MESSAGE` (the panel shows either
across itself for eight seconds), `power_menu` sends `power-menu OUTPUT`, and `taskbar_focus`
sends `taskbar OUTPUT` for the focused monitor, whose panel takes the keyboard to walk its
buttons, or gives it back. Children of the session find the socket through `SHAODESK_SOCKET`. Actions are
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

The power button in the bottom-right corner of the start menu, as on Windows, opens a menu above
it of what may run now, each with its icon: Lock screen (with a locker installed),
Suspend, Hibernate, Restart, Power off (as logind allows them) and Log out; the button is left
out when nothing may. A click elsewhere in the start menu closes only the power menu. Its entries, and what is refused, come from the
compositor, which also reports across the panel an action that fails or is cancelled later.
Restart, Power off and Log out ask first: a dialog in the middle of the monitor counts down
`power.countdown` seconds (10; 0 waits for a click) and then goes ahead, as do its button and
Enter, while Escape, Cancel or a click beside it gives up. The keyboard starts on the action's
button, ringed, and Tab moves it to Cancel, where Enter gives up. The actions themselves, bound to keys
or sent with `shaodesk msg`, do not ask. The `power_menu` action opens the start menu
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
workspace; the taskbar's pictures of windows are taken the same way. While the screen is
locked no capture of a window can start: a program asking is told its capture stopped.

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
