# Architecture

How the code is laid out, and which files a change usually touches. [CONTRIBUTING](../CONTRIBUTING.md)
covers branches, building, testing and committing; [features.md](features.md) describes behavior.

## Processes

- **`shaodesk`** (`src/main.cpp`) is the compositor. `main.cpp` loads the Lua configuration,
  watches it, runs scripts (screenshots, spawned programs) and answers the compositor's
  questions through a table of callbacks (`struct sh_callbacks` in
  `include/shaodesk/backend.h`). Then it hands over to `sh_run`, the C compositor in
  `src/compositor/`. `shaodesk msg ...` is the same binary acting as a control client.
- **`shaodesk-shell`** (`shell/`) is the Qt Quick shell: panels, launcher, notifications, tray, OSD,
  and the text of the switcher and overview. It is an ordinary layer-shell client that
  connects to the control socket with `subscribe` and gets state lines and events.

## Layers

| Where | Language | What |
| --- | --- | --- |
| `src/compositor/` | C | The compositor proper, on wlroots. Shares one private header, `server.h`. |
| `src/*.c`, `include/shaodesk/*.h` | C | Pieces the compositor uses that stand on their own: animations, window controls, shadows and tab strips (pixels), effect arithmetic, overview thumbnails, session files, the logind client. Several are unit tested. |
| `src/config.cpp`, `src/config_schema.cpp` | C++ | The Lua configuration: parsing, validation, the action table and the settings schema. |
| `src/tiling.cpp`, `src/layout.cpp`, `src/window_placement.cpp`, `src/overview_layout.cpp` | C++ | Pure geometry (tiling layouts, snapping, placement, the overview grid), with a C interface in `backend.h` and unit tests. |
| `src/import/` | C++ | `shaodesk import` from Hyprland and Waybar. |
| `shell/` | C++/QML | The shell. |

Keep geometry and arithmetic out of `src/compositor/` where you can: a pure function in its own
file can be unit tested without starting a compositor.

## The compositor (`src/compositor/`)

Each file begins with a comment saying what it covers; `head -4 src/compositor/*.c` shows them
all. In short:

| File | Covers |
| --- | --- |
| `server.c` | Startup (creating every wlroots global and listener), shutdown, config reload, signals. |
| `server.h` | The shared types (`sh_server`, `sh_output`, `sh_toplevel`, ...) and, under a `/* file.c */` heading, every function one file calls in another. |
| `actions.c` | `run_action`: one `case` per action, handing it to the module that does it. |
| `control.c` | The control socket: reading requests, commands that are not actions, subscribers and shell events. |
| `query.c` | `shaodesk msg get ...`: one function per query, and the table that names them. |
| `input.c` | Keyboards, key bindings, pointers' libinput settings, virtual devices, selection and drag-and-drop. |
| `keymap.c` | The keymap from the keyboard settings, given to every keyboard but virtual ones. |
| `cursor.c` | What is under the pointer, focus on hover, button bindings, scrolling, the cursor image. |
| `grab.c` | Moving and resizing with the pointer, magnetic edges, dropping. |
| `focus.c` | Keyboard focus and urgent windows. |
| `toplevel.c` | Windows: xdg-shell toplevels and popups, opening by window rules, maximize, fullscreen, minimize. |
| `xwayland.c` | X11 windows and the XWM waker. |
| `frame.c` | Borders, opacity, rounded corners, window controls, shadows, tab strips. |
| `placement.c` | Snapping, maximizing, reflowing, moving and resizing by keyboard. |
| `tiling.c` | Glue between windows and the layouts in `src/tiling.cpp`. |
| `workspace.c` | Workspaces per output, sticky windows. |
| `output.c`, `output_moves.c` | Monitors and their configuration; windows and workspaces moving between outputs. |
| `layer_shell.c` | Panels and other layer surfaces. |
| `group.c`, `scratchpad.c`, `swallow.c`, `switcher.c`, `overview.c`, `session.c` | One feature each. |
| `effects.c` | Dimming, peeking at the desktop or at one window, night light, magnifier, hot corners. |
| `lock.c` | Session lock and idle/sleep inhibitors. |
| `power.c` | The power actions: suspend, hibernate, reboot and power off through logind (`src/login1.c`), locking first, closing windows first, log out. |
| `foreign_toplevel.c` | Window lists for taskbars and single-window capture. |
| `window_control.c` | The shell's window menu and window pictures: shaodesk-window-control-v1, which names a window by its taskbar handle. |
| `scaled_capture.c` | The capture source for a window's picture: the window scaled down to fit a size on the renderer, smoothly. |

A function used by one file is `static`; one used by several is declared in `server.h` under
the file that defines it. The build warns (`-Wmissing-prototypes`) about one that is neither.
Code that only exists with XWayland is inside `#if WLR_HAS_XWAYLAND`.

### Naming a window from the shell

The shell knows windows only as wlr-foreign-toplevel handles, which carry a title and an app id
but no name the control socket could be asked about, and two windows may share both. Its window
menu reaches the compositor through a protocol of shaodesk's own,
`protocols/shaodesk-window-control-v1.xml`, whose requests name a window by the handle the shell
already holds, on the same Wayland connection. `shaodesk_window_control_v1.get_window(handle)`
gives a `shaodesk_window_v1` that sends the window's `output` (connector name), `workspace` (from
1) and `state` (sticky, floating, tiled, and whether its workspace tiles), then `done`, at once and
again after each change, and takes `move_to_workspace`, `move_to_output`, `set_sticky` /
`unset_sticky` and `set_floating` / `unset_floating`. Since version 2 its `get_capture_source`
gives the window's `ext_image_capture_source_v1`, and since version 3
`get_scaled_capture_source(width, height)` gives one whose frames the compositor scales down to
fit that many buffer pixels; the shell copies them with ext-image-copy-capture for the taskbar's
pictures of windows.

`window_control.c` finds the window by looking for the handle among the resources of each
window's `wlr_foreign_toplevel_handle_v1`, so a handle that is gone names nothing and its object
is inert; `unpublish_toplevel` makes a window's objects inert as its handles close. The requests
call what the actions do for the focused window (`move_toplevel_to_workspace`,
`move_toplevel_to_output`, `set_sticky`, `set_floating`), so they neither focus nor raise it,
and do nothing while the session is locked. A change reaches the objects through
`window_objects_changed`, which `notify_subscribers` and the tiling call: it sends what changed
from an idle callback, once the change is over. Like the foreign-toplevel manager, the global is
offered to every client; it lets a client do nothing to a window a taskbar cannot already do.
`tests/window_probe.c` is a client of it for `window_control_smoke`, `window_capture_smoke` and
`window_peek_smoke`, and `TaskModel` the shell's.

A window's capture source is the one screen sharing gets for its ext-foreign-toplevel-list
handle, from `toplevel_capture_source` (`foreign_toplevel.c`): made from a private scene that
holds only the window's surfaces (`capture_scene`, which `publish_toplevel` builds), when first
asked for, and shared by every client; it renders the window at its logical size without its
frame, also while it is minimized or on a workspace not shown, and its sessions stop as the
window's handles close. While the session is locked, or once the window is gone, both ways give
an inert source, whose sessions stop at once, rather than none: a client's next request would
then name an object that does not exist, a protocol error that disconnects it. wlroots renders
the source again only where the window changed since it last did, and for every session at
once: a session started while another runs on the same window gets its first frame only when
the window next draws, so a client keeps one session per window.

A scaled capture source (`scaled_capture.c`, adapted from wlroots' source for a scene node)
renders the same capture scene through an output of its own, a `wlr_output` on an empty backend
that no client is offered, at a scale below 1. Each request makes one, so no two clients'
sessions share it. It goes once its client has destroyed the resource and no session
captures it, from an idle callback since wlroots stops a source from inside a session's
teardown, or with the capture scene as the window closes, which stops its sessions. A capture
scene takes 16 outputs at most (wlroots' scene asserts below 64); a request past that gets an
inert source. The renderer samples bilinearly without mipmaps, so one pass from a large window
to a picture a tenth its size would leave out most of its pixels, and text would shimmer as it
changes. Instead the scene renders at no less than half the density of the window's buffers,
then steps halve that on the renderer, each pixel the average of two by two
(`wlr_render_pass_add_texture` into a swapchain of each step's own), until it is within twice
the frame's size, and a last bilinear pass makes the frame. A frame's pixels thus stand for all
those they cover, so the shell asks for the size it shows and scales nothing. The steps' buffers
last while a session captures; a buffer the size of the output, such as a window's own at its
logical size, is taken as it is (direct scan-out). A frame is rendered when a session asks for
one and the window changed since, so a window that stops drawing costs nothing, and at most once
for each frame asked for, which paces a window shown nowhere else. A session owed what changed
since its last frame, one started while another runs or one that let frames go by, gets the frame
as it stands at once (the last one kept for it) rather than waiting for the window to draw.
`shaodesk msg get pictures` lists these sources.

Measured on a headless compositor with the pixman renderer, a window of 3840 by 2108 pixels that
redraws whenever it may, minimized, with the card of its 240-pixel picture open: a frame was
32.4 MB at the window's size and is 127 KB scaled. The shell took 9 to 11 % of a core for ten
full-size frames a second, 0.7 % for ten scaled ones and 2.3 to 2.5 % for thirty, most of it
Qt Quick drawing the card in software. The compositor took 12 to 13 % for ten full-size frames,
10 % for ten scaled and 26 to 31 % for thirty, pixman rendering the steps on the CPU; a GPU
renderer does that on the GPU, and copies 127 KB back where it copied 32 MB.

Since version 3 a `shaodesk_window_v1` also sends the window's `pid` with its state, before
`done`: `toplevel_pid` (`toplevel.c`), the Wayland client's peer credentials or the process XRes
names for an X11 window, 0 when unknown, for the shell to match the window with the sound
server's streams.

Since version 4 `set_peek` peeks at the window and `unset_peek` ends the peek (`effects.c`), for
the taskbar's card while the pointer rests on a picture. The other windows fade with
`peek_fade`, the desktop peek's, while the window's own `peek_shown` fade, which `peek_scale`
(the opacity `refresh_frame` gives its buffers) weighs against it, keeps it in full. It is drawn
over them from `peek_layer`, a tree between the fullscreen windows and the top layer, so that the
bar and the card stay over it, while an empty tree keeps its place among the windows; one
fullscreen over the panels stays where it is, over the others already. A hidden window
(minimized, or on a workspace its output does not show) has its node shown for the peek where it
is (`peek_lent`), fades in and out, and is hidden again once it has faded out (`tick_effects`);
`scene_node_at` passes over it, so it takes no input. Moving the peek to another window keeps the
others faded and crossfades the two; ending it fades the others back, a window shown anyway
staying in full until they are, and puts the window back in its place unless it was moved on
purpose meanwhile, as focusing raises it. Nothing else about the window changes. The peek ends
as the object that asked is destroyed (its client too), as the window closes
(`forget_window_peek` in `unmap_toplevel`) or leaves the taskbar (`window_objects_forget`), is
focused (`focus_toplevel_raise`, which a click on its picture does), as the session locks (at
once), and as the desktop peek starts. `shaodesk msg get window_peek` lists what it does to each
window, and `tests/window_peek_smoke.py` tests it.

## The shell (`shell/`)

`ShellController` (`controller.cpp`) loads the configuration, keeps the compositor's state from
its control socket and holds the models; QML reaches it as the context property `shell`. It
starts applications: `shell.launch(id)`, and `shell.appActions(id)` with
`shell.launchAction(id, action)` for the actions a desktop entry offers besides starting it
(`[Desktop Action …]`, listed as `{action, name, icon}`), each failure shown across the panel.
`view.cpp` makes one Qt Quick window per surface and output (a layer surface each), all in one
QML engine. The QML is compiled into the binary (`qt_add_qml_module` in `shell/CMakeLists.txt`,
which lists every file).

The taskbar's popups are drawn in a surface of their own, a `PopoverWindow` (`view.cpp`) that
`Panel.qml` declares, so they stay in the panel's QML tree and state while the bar's surface
keeps its size. It is an overlay layer surface covering the output (exclusive zone -1, so its
coordinates are the output's), above fullscreen windows too, and hidden while nothing is open.
While a menu or popup is open it holds the keyboard and takes every press but those on the bar's
strip, where `inputRects` leaves a hole: a press on another bar button still switches popups in
one press, and a press beside the popups closes them. The windows of a button shown on hover (the
card of their pictures, or a stack's list) take only the pointer over the card and down to the bar
(`hoverArea`), where a drag reaches them too, and leave the keyboard where it is. Losing the
keyboard while it holds it (`dismissed`) closes the popups. Without layer shell (`--preview-popup`,
`shell_ui_test`) it is an ordinary window as large as `ShellView::previewSize()`, and a preview's
screenshot draws it over the bar.

`Panel.qml` loads the bars of `shell.style`. The taskbar (`Taskbar.qml`) fills the panel's surface.
The macOS style has two: the dock (`Dock.qml`) in the panel's surface, which is then at the bottom
whatever `shell.panel_position` says and reaches above the strip it reserves by half its height,
room for an icon to bounce in; and the menu bar (`TopMenuBar.qml`) in a `MenuBarWindow`
(`view.cpp`) that `Panel.qml` declares as it declares its popover: a layer surface on the top
layer along the output's top edge, reserving its strip, shown only in that style, in the panel's
QML tree and state. The panel's surface then takes the pointer only over the dock (`ShellView`'s
`inputRects`), so the desktop beside it and under that room stays reachable, and the popover leaves
holes for both bars (`popoverInput`), but takes every press while Launchpad covers them. A popup
of the bar with the widgets opens by `panel.barAnchor`, below the menu bar in the macOS style;
one of the dock (an application's menu, the windows of one) by `panel.dockAnchor` and
`panel.dockSide`, which on the taskbar are the bar's. A change of style makes and drops the
bars, and the surfaces follow at once.

The window switcher, the command palette, the power dialog and the overview's text are
`OverlayView`s (`view.cpp`), a layer surface each on every output's overlay layer. `present()`
shows one and sets its QML root's `shown`; `dismiss()` clears it, and the root animates its own
`progress` back to 0 as its transition from the "shown" state says, the view hiding once it is
there (at once with animations off). While it goes it is transparent for input and gives up the
keyboard, so the windows under it have both at once, and shown again it comes back from where it
was. What the compositor forgets as one closes (the switcher's windows, the power dialog's
question, the overview's search) its root holds with a `Binding` while `shown`, so the fade shows
what was there.

| File | Covers |
| --- | --- |
| `Theme.qml` | The design tokens (colours, type, radii, spacing, icon sizes, motion, whether effects can be drawn), derived from the appearance profile. A singleton: every file reads `Theme.surface`, `Theme.hover`, ... instead of colours and sizes of its own. |
| `Panel.qml` | The panel on one output: which popup is open and where, the bars of the style (a loader for each), and the popover with a loader for each popup. Every part below takes the panel as `panel` (and a popup the bar as `barItem`) and reaches its state and functions through it; a popup is placed in `panel.popupLayer`, beside the part of the bar it belongs to (`panel.barAnchor(x, width)`). |
| `Taskbar.qml` | The taskbar: the bar along the panel's edge with the start button, the pinned applications, the windows, the widgets and the clock, and its smaller buttons. |
| `TopMenuBar.qml`, `Dock.qml`, `DockIcon.qml` | The bars of the macOS style (`shell.style`): the menu bar along the top in a `MenuBarWindow` of its own, with the system, application and Window menus, the widgets, search, Quick Settings and the clock; and the dock in the panel's surface, an icon for each application, pinned or running, with the applications button and the Trash. |
| `BarKeyboard.qml` | The keyboard on the bar (`taskbar_focus`): held in the popover, it walks the taskbar's buttons and the windows they show. |
| `PinnedSlots.qml`, `TaskList.qml`, `TaskButton.qml`, `TrayButton.qml`, `WorkspaceIndicator.qml`, `VolumeButton.qml`, `ClockButton.qml`, `BatteryWidget.qml`, `NetworkWidget.qml`, `NotificationBell.qml`, `KeyboardLayout.qml`, `QuickSettingsButton.qml`, `WallpapersButton.qml`, `ProfilesButton.qml`, `TilingButton.qml`, `BarAppIcon.qml`, `Badge.qml`, `BarTip.qml` | Parts of the bar: widgets, an application's icon on it, a count on a pill, and the tooltip for things on it. |
| `ClockFlyout.qml`, `QuickSettings.qml`, `AudioMixer.qml`, `AudioOutputs.qml`, `ProfileList.qml`, `WallpaperPicker.qml`, `Launcher.qml`, `PowerMenu.qml`, `TaskbarMenu.qml`, `TrayMenu.qml`, `GroupList.qml`, `WindowThumbnails.qml`, `MenuBarMenu.qml` | Popups of the bar, each made by a loader in `Panel.qml` when first needed. |
| `CalendarPopup.qml`, `NotificationHistory.qml` | The clock flyout's cards: the month calendar, and the notifications grouped by application. |
| `QuickTile.qml` | A tile of Quick Settings: a toggle, a list it opens, or a state. |
| `Icon.qml`, `FadingIcon.qml`, `SpeakerIcon.qml`, `BatteryIcon.qml` | Line icons (Lucide), drawn as vectors in any colour; one that crossfades as the state it shows changes, the loudspeaker for a volume, and a battery filled to its charge. |
| `FlatButton.qml`, `ButtonFill.qml` | The frameless button of the bar and of menus, and its background, which fades between the hover, pressed and active states. |
| `PushButton.qml` | A framed button with text: raised, or filled for what a click mostly does or for a destructive action, with a ring for the keyboard; a dialog's, a notification's, the start menu's. |
| `FocusRing.qml` | The ring that says the keyboard is at something, as `PushButton`'s: around a button on the bar, a picture or a row the keyboard on the bar selects. |
| `TextButton.qml` | A button that is only its text in the accent colour, as Today and Clear all over a card's list. |
| `CloseButton.qml` | The round cross that closes a card, a notification or a window in a stack's list, or clears the search. |
| `SearchInput.qml` | The start menu's and the command palette's search field. |
| `EmptyState.qml` | What a list says while it has nothing to show: an icon, a line and a hint. |
| `PopupCard.qml` | A popup's card: surface, outline, corners, a shadow through the GPU, the open and close animation, and its place beside what it belongs to. |
| `PopupMenu.qml`, `MenuRow.qml` | A menu of plain entries on popup cards, with cascading submenus and keyboard navigation, and one row of it. |
| `WindowMenu.js` | The entries of a menu about windows that move them to another workspace or monitor. |
| `AudioSlider.qml`, `MuteButton.qml`, `StreamRow.qml` | Controls the mixer and Quick Settings use: a volume's slider, a mute button, and an application playing sound. |
| `StartHome.qml`, `StartAllApps.qml`, `StartSearch.qml`, `StartBestMatch.qml`, `StartTile.qml`, `StartRow.qml`, `UserAvatar.qml` | Parts of the start menu (`Launcher.qml`): its pinned and recent applications, every application from A to Z, what its search finds and the best match of it, a pinned application, a row of its lists, the user's picture. |
| `Launchpad.qml` | The launcher of the macOS style, in the start menu's place: every application on pages of a grid over the whole output, with a search. |
| `Desktop.qml` | The wallpaper and the desktop's launchers, on the background layer. |
| `DrawnWallpaper.qml` | The wallpaper the macOS style draws while none is set, light or dark, from the background colour and the accent. |
| `Switcher.qml`, `Overview.qml`, `Palette.qml`, `PowerDialog.qml`, `NotificationCards.qml`, `Osd.qml`, `ConfigError.qml` | One overlay surface each. |

The models behind them: `task_model.cpp` (windows, from foreign-toplevel) and `task_filter.cpp`
(the taskbar's slots and groups), `audio.cpp` with `pulse_audio.cpp`, `system_status.cpp`
(battery, network), `tray*.cpp`, `notification*.cpp`, `osd.cpp` and `backlight.cpp`,
`power.cpp`, `palette.cpp`. `preview.cpp` has stand-ins for all of them for
`--preview-popup`.

The pictures of the windows on the taskbar's card (`WindowThumbnails.qml`, with `shell.thumbnails`)
are `TaskModel`'s: its `picture` role is `image://windows/<taskId>/<serial>` once a window has one,
`""` until then, the serial new with every picture so that an `Image` with `cache: false` loads it
again, and the `windows` image provider serves them by task id. The model takes pictures of a
window only while something watches it: `watchPicture(taskId, pixelWidth, live)`, counted, until
`unwatchPicture(taskId)`. It asks the window control for the window's capture source scaled down to
fit `pixelWidth` by 5/8 of it (`get_scaled_capture_source`, version 3; a new width asks again), or
with version 2 for it at the window's size (`get_capture_source`), captures it with
ext-image-copy-capture into shared memory, scales the picture down off the GUI thread where the
frame is larger than that (only ever a copy with version 3), and, when `live`, takes the next as
the window redraws, every 33 ms at most (100 ms at the window's size). The last picture stays until
the window closes, so the card opens with it.

`Panel.qml` alone calls them, for what tells it that it wants a window's picture with
`wantPicture(taskId, wanted)`: a tile of the card as it appears and as it goes, and the panel
itself for the windows of the button the pointer has rested on for half of `shell.thumbnailDelay`
(`warmGroup`, until the card opens or the pointer leaves), so that the card opens on their
pictures. It counts the wants and asks the model once a change is over (`syncPictures`), so that a
window the tiles take over from the panel as the card opens is neither let go nor asked for
again, which would start its capture anew, or without `live` take its one picture twice. A
stand-in model (the preview's, the tests') has no `watchPicture`, which the panel then does not
call, and names pictures of its own (`image://preview-windows/ID`, painted by `preview.cpp`).

A drag from an application never reaches the hover handlers: Wayland sends its events
(`wl_data_device`'s enter, motion and leave) to the surface under the pointer instead, and the
pointer stays with none while it lasts. `Panel.qml` follows it with a `DropArea` over the panel's
surface and one over the popover's (`dragOverBar`, `dragOverPopover`), which find the button under
it (an item with `dragWindows()`: a `TaskButton` or a `DockIcon`) or the tile or row of a window
on the open card or list (an item with `taskId`, `active` and `minimized`). After `dragDelay` a
button's one window is activated (`taskSource.activate`, unless it is in front already, which that
would minimize), a stack's windows shown (`openGroup`), or a tile's window activated. What is
open stays while `dragHolds`, which `groupHide` reads beside the hover state. Each `DropArea`
refuses every move (`drag.accepted = false`), and Qt answers the drag's source with the move's
answer, so the shell never takes a drop and the source sees it cancelled. Each fills its surface,
so that a drag enters it only as it comes onto the surface, when Qt follows the enter with a move
at once; an item entered by a later move would answer that move with its enter, which takes the
drag. `shell_ui_test` hands the windows drag events as Qt's Wayland platform does. A drag holds the keyboard in the compositor, and wlroots
lets no focus change through it: `drag_ended` (`input.c`) gives the keyboard to the window
focused meanwhile once the drag ends, and the pointer to the surface under it.

The speaker on a picture's tile is a `WindowSound` (`audio.cpp`), which the tile makes with the
panel's `audioSource` and `taskSource` and its window's `pid` (`TaskModel`'s role, from the window
control's version 3). The sound server's backend gives each stream the process that plays it
(`application.process.id`, or its client's when the stream names none, as a PipeWire
application's may not) and that process's parents, nearest first, read once per stream from
`/proc/PID/stat` by `processAncestry`, 64 at most and stopping before pid 1, as `swallow.c`
walks them. `soundOwner` gives a stream to the nearest of those processes that has a window among
the task source's `pid` roles, so a browser's audio process plays in its window and a player
started in a terminal in the terminal's, unless the player has a window. `playing` is a stream of
the window's that is open and not paused (corked) or muted, `muted` one that is muted while none
plays, and `toggleMute()` mutes or unmutes all of them through `Audio::setStreamMuted`. The
preview's and the tests' stand-ins give the `pid` roles, and the streams' `processes`, themselves.

A tile peeks at its window while the pointer rests on its picture: a `HoverHandler` on the
picture's box and a timer call the task source's `peek(taskId)` after 500 ms, or at once while
its `peekedTask` names a window already, so that the peek moves straight to the next picture, and
`endPeek(taskId)` 150 ms after the pointer leaves the picture or the card closes, time to cross to
the next one, whose peek takes over first (`endPeek` ends only the peek at that window). Half a
second is about Windows' wait, a little longer than the card's own, so that crossing a picture on
the way to the cross, the speaker or another tile does not fade every window on the screen.
`TaskModel` sends `set_peek` and `unset_peek` (version 4) for them; the compositor ends a peek by
itself too, as the window is focused (a click on the picture), which the model does not hear, so
`peekedTask` names the last window it peeked at until `endPeek`. A stand-in without `peek` peeks
at nothing, and the macOS style's dock and the stack's list of titles do not peek.

The start menu (`Launcher.qml` and its `Start*.qml` parts) reads `shell.startMenu`, a `StartMenu`
(`start_menu.cpp`): its own pins, seeded from the taskbar's; the applications launched lately
(`launch_history.cpp`, which the controller tells of every launch); every application by letter;
its search, which takes the palette's windows, workspaces and actions from `Palette::entries` and
runs them with `Palette::run`; and the user's name and picture. It tells the controller to read
the applications again when GIO's monitor says they changed. Launchpad (`Launchpad.qml`), the
launcher of the macOS style, which the launcher's loader in `Panel.qml` makes in the start menu's
place, reads it too: every application by name, and what its search finds of them.

### Popups and menus

A popup is a `PopupCard`; a menu is a `PopupMenu` of plain entries. Both place themselves beside
an anchor rectangle, so a popup of the bar only says what it belongs to.

`PopupCard` draws the card (`Theme.popupSurface`, outline, `radius`, a shadow when `Theme.effects`;
none with `framed: false`, for content that draws cards of its own) and
fades in with a few pixels' slide from its anchor while `open`, out again when it is cleared,
staying visible until it has; `progress` is how far open it is. Its content goes inside it and
fills it. It places itself beside `anchorRect` (in its parent's coordinates) on the anchor's
`side` (`Qt.TopEdge`, `Qt.BottomEdge`, `Qt.LeftEdge`, `Qt.RightEdge`), `gap` away, lined up by
`alignment` (`Qt.AlignHCenter`, `Qt.AlignLeft`, `Qt.AlignRight`, or the vertical ones beside the
anchor); it flips to the other side when that one has more room (`placedSide` says where it went),
and stays `margin` inside `bounds`. Its size is its `implicitWidth` and `implicitHeight`, cut to
`availableWidth` and `availableHeight`. With `glides` (the card of window pictures), a card that
is shown eases to another place or size (`placedX`, `placedY`, `placedWidth`, `placedHeight`)
rather than jumping, and one that is not shown takes it at once, so that opening and closing keep
their own motion. `opened()` is emitted once each time it opens and shows,
when its content resets, and `initialFocus` (the card, unless set; `null` for none) then takes the
keyboard. Presses on it stay with it until it starts closing. With `anchored: false` it is only
the card, for a surface that places it itself.

`PopupMenu` fills its parent and draws its levels on popup cards. Its `entries` are objects:

    { text, icon, secondary, toggle, checked, enabled, danger, run, submenu, objectName }
    { separator: true }
    { header: "Section" }
    { title: "Name", icon, secondary }

`icon` is a Lucide name `Icon.qml` knows, else a theme icon's name or an image's URL; `secondary`
is muted text at the row's end (a shortcut, the value in use). A `title` is the menu's own heading
above its entries: the icon large, the name, and the secondary text under it, elided to the width
the entries need; `toggle` is `"check"` or `"radio"`
with `checked`; `enabled: false` greys an entry out and `danger: true` draws it in the danger
colour. `run` is called when the entry is chosen, and the menu then emits `dismissed()` unless it
returns `true`; `triggered(entry)` comes first. `submenu` is an array of entries, or a function
returning one that is read as it opens and again when what it read changes. Placement is the
card's: `anchorRect`, `side`, `alignment`, `gap`, `bounds`; `minimumWidth`, `maximumWidth` and
`rowHeight` size the rows. A submenu opens beside its entry after the pointer rests there for
`submenuDelay` milliseconds, on a click, or with Right, Enter or Space; the pointer heading for an
open submenu across other entries (inside the triangle from where it was to the submenu's near
edge) leaves it open until it rests on one. Up, Down, Home and End move, Left or Escape closes a
submenu and Escape in the first level emits `dismissed()`. In the macOS style the rows are denser and
the highlighted one is filled with the accent, its text white, as macOS draws menus (`Theme.menu*`).
`initialIndex` highlights an entry as it opens (-1, none, for a menu opened with the pointer).
`openEntries` lists the entries whose submenus are open, `openSubmenu(index)` opens one,
`closeSubmenus()` closes them, and `card` is the first level's card. Rows are `MenuRow`s, named
`entryName` (`separatorName` for separators) unless an entry gives its `objectName`.

### A new popup of the taskbar

1. In `Panel.qml`, a property saying whether it is open (or a value of `audioPopup`), part of
   `menuOpen` if it takes the keyboard, cleared by `closeMenus()`, and a function that opens it
   by a bar item as `toggleAudioPopup` does: noting where (`audioPopupX`) and closing the others.
2. Its file: a `PopupCard` or `PopupMenu` with `required property var panel` and
   `required property Item barItem`, `parent: panel.popupLayer`, `open:` that property,
   `anchorRect: panel.barAnchor(x, width)`, `side: panel.popupSide` and
   `bounds: panel.popupArea` (the output but the bar), so that it opens away from the bar on a
   top panel as on a bottom one, and below the menu bar in the macOS style; one that belongs to
   the dock's icons takes `panel.dockAnchor(x, width)` and `panel.dockSide` instead. A menu's
   `onDismissed` clears the property.
3. A `Loader` for it in the popover in `Panel.qml`, like the others: made when first opened or a
   moment after startup (`root.warm`), kept once made.
4. The file in `QML_FILES` in `shell/CMakeLists.txt` and in the table above; a name in
   `previewPopup` (`Panel.qml`; `previewMacos` for one only the macOS style has, which goes in
   `MACOS_POPUPS` too), `--preview-popup`'s help (`main.cpp`) and `POPUPS`
   (`tools/shell_gallery.py`), and a look at the gallery's pictures.
5. Tests in `shell_ui_test`: `find(view.rootObject(), NAME)` finds the popover's items too,
   `click(item)` clicks one in its own window, `inPopover(popup)` says it is open, settled and
   inside the popover, and keys go to `popover`.

### Drawing something in the shell

Take every colour, size and duration from `Theme`: `bar`, `surface`, `surfaceRaised` and
`surfaceRaisedHover` for backgrounds; `hover`, `pressed` and `selected` laid over them for
states; `border` and `divider`; `text`, `textMuted` and `textDisabled`; `accent`,
`accentHover`, `accentSubtle` and `textOnAccent`; `danger`, `dangerFill`, `textOnDanger` and
`dangerSurface`; `urgent` and `urgentSubtle`; `scrim`. Type is `fontFamily` with `fontSize`,
`fontSizeCaption`, `fontSizeSmall`, `fontSizeLarge`, `fontSizeTitle` and `fontSizeDisplay`;
shapes `radiusSmall`, `radiusMedium` and `radiusLarge`; spacing `spacingXS` to `spacingXXL`
(2, 4, 8, 12, 16, 20); icons `iconSizeSmall`, `iconSize`, `appIconSize` and
`appIconSizeLarge`; rows of menus and lists `rowHeight`, their headings `headingHeight`; buttons
on the bar `barButtonHeight` and, an icon's, `barButtonWidth`. Animations use `durationFast`,
`durationNormal`, `durationSlow` or `duration(ms)` with `easing` or `easingExit`, all 0 while
`animations.enabled` is off; something small growing in starts at `growFrom` of its size, and an
icon on the bar shrinks to `pressScale` while pressed. Nothing animates while nothing changes,
so that an idle shell wakes for nothing: motion is a `Behavior` or a transition on a change, and
an animation that runs by itself ends, as the urgent pulse does after a few beats. `effects` says whether shader effects
(shadows) can be drawn: only through the GPU, so draw them only when it is true, as a popup's
`shadow` colour, `shadowBlur` and `shadowOffset` are. `alpha()` and `mix()` derive a colour from
these. A button without a frame of its own is a `FlatButton`, one with a frame and text a
`PushButton`, a tooltip for something on the bar is a `BarTip`, a popup is a `PopupCard` and a
menu a `PopupMenu`.

`macos` says whether the macOS style (`shell.style`) is in use. Its popups have tokens of their
own, after `shadowMargin`: `popupSurface`, `popupOutline` and `popupInnerEdge` for a popup's card,
`textOnAccentFill` for text on the accent, and `menu*`, `launchpad*`, `spotlight*`, `module*`
(Control Center's cards), `notification*`, `switcher*`, `button*`, `field*`, `slider*`, `knob`
and `switchTrack`. One that stands for a token above is that token in the taskbar style, so a part
draws with it in both styles and the taskbar's keeps its look; branch on `Theme.macos` only where
the macOS layout differs (Control Center's modules, Spotlight's groups), and give a popup a file of
its own only where all of it does (Launchpad).

### Seeing a change

`shaodesk-shell --config FILE --preview-popup NAME --screenshot OUT.png --quit-after 400`
renders the taskbar offscreen with one popup open, on stand-in windows, sound, tray items and
notifications, over the configured wallpaper (`--preview-popup` lists the names); in the macOS
style the dock and the menu bar, over the style's drawn wallpaper while none is set, with the
menu bar's menus as `system-menu`, `app-menu`, `window-menu` and `window-submenu`. The overlay
surfaces have names there too (`osd-volume`, `osd-text`, `cards`, `power-dialog`, `palette`,
`palette-empty`, `switcher`, `overview`): `PreviewData` in `preview.cpp` shows one in a window of its own over the
bar alone, with stand-ins for what the compositor would tell it, and the screenshot draws it where
its layer surface would be (the overview over stand-ins for the compositor's thumbnails).
`--preview --preview-desktop` draws the desktop instead (the gallery's `desktop`, without the
wallpaper it sets for the others, so that the macOS style's drawn one shows).
`tools/shell_gallery.py BUILD_DIR OUT_DIR` does that for every popup in a light and a dark
theme of each style (`light`, `dark`, `macos-light`, `macos-dark`), both with the software renderer (`light-launcher.png`) and through the GPU
(`light-launcher-gpu.png`: Qt's OpenGL on Mesa's software implementation, in a private headless
compositor), in about ten seconds; `--renderer`, `--theme` and `--popup` narrow it down. Nothing
touches the session it runs in. The `shell_gallery` test runs it and fails on any QML warning.

## Recipes

### A new binding action

1. Add `SH_<NAME>` to `enum sh_action` in `include/shaodesk/backend.h`.
2. Name it in `action_table` in `src/config.cpp`. That makes it bindable, usable from
   `shaodesk msg` and hot corners, and listed in the configuration reference.
3. If it takes an argument, parse it in the `command` callback in `src/main.cpp` (the
   `action_takes_*` helpers in `config.cpp` say which kinds exist); otherwise it is refused
   with "takes no argument". One that starts a program goes through the `launch` callback
   there, as `spawn` and `terminal` do, so a failure reaches the panel.
4. Add a `case` to `run_action` in `src/compositor/actions.c` that calls the module doing the
   work. If it acts on the window under the pointer when bound to a button, list it in
   `action_targets_window` in `cursor.c`.
5. Mention it in `README.md` or `docs/features.md`. The `docs_consistency` test fails until
   you do.

### A new setting

See "Lua settings" in [CONTRIBUTING](../CONTRIBUTING.md#lua-settings): an entry in
`src/config_schema.cpp`, parsing in `src/config.cpp`, a field in `struct sh_settings`
(`backend.h`), then regenerating `docs/config-reference.md`. The compositor reads settings
through `server_settings(server)`, and reacts to a reload in `reload_config` (`server.c`).

### A new query (`shaodesk msg get NAME`)

Write `static void get_<name>(struct sh_server *server, int fd, const char *arguments)` in
`src/compositor/query.c` and add it to `queries[]`. Reply with `control_reply(fd, "ok\n")`
followed by tab-separated lines, or a line starting `error: `. `arguments` is NULL unless the
table entry accepts them. Queries answer while the session is locked, so they must not change
anything. Tests use queries to see the compositor's state, so a feature that is hard to test
often wants one.

### A new control command that is not an action

Handle it in `control_handle` in `src/compositor/control.c`, after the session-lock check,
following `dnd` or `osd`.

### A new Wayland protocol or global

Create the global in `sh_run` in `server.c`, keep its pointer and `wl_listener`s in
`struct sh_server` (`server.h`), and put the handlers in the module the protocol belongs to,
declaring the ones `server.c` connects in `server.h`. Add the `#include` for its wlroots
header to `server.h`. A protocol of shaodesk's own is an XML file in `protocols/`, named in the
list `CMakeLists.txt` runs wayland-scanner on (server header and code for the compositor,
client header and code for the shell and the test probes), its global made with
`wl_global_create`, as `window_control.c` does.

### Telling the shell something

`send_event` (`control.c`) sends a line to every subscriber; `send_shell_line` and
`request_shell` are the shell-specific forms, and `report_failure` logs a failure the user no
longer waits on and shows it across the panel (`power-error`, `spawn-error`). The shell reads
them in `shell/controller.cpp`.

### A new test

- A pure function: a unit test next to the others in `tests/*_tests.c(pp)`, registered with
  `add_executable` and `add_test` in `CMakeLists.txt`.
- Compositor behavior: a smoke test, `tests/<name>_smoke.py`. Copy a short one such as
  `sticky_smoke.py`. `with harness.Compositor(compositor, CONFIG) as desktop:` starts a headless
  compositor with the pixman renderer in a temporary `XDG_RUNTIME_DIR`; open windows with
  `desktop.spawn([probe, ...])` (`wayland_probe` or `x11_probe`), drive it with `desktop.msg`,
  and read the state back with `get` queries (`desktop.rows("windows")`). On the way out it ends
  every client, checks that the compositor exits cleanly, and prints the logs if the test
  failed. Wait with `desktop.wait_for`, never with a fixed sleep; to check that something does
  not happen, which an animation or a client's commit could do a little later, use
  `desktop.stays`. Register it with `add_test` and a `TIMEOUT` under
  `SHAODESK_BUILD_COMPOSITOR` in `CMakeLists.txt`. A temporary directory's prefix stays at 26
  characters or fewer: the control socket goes in it, a Unix socket's path is limited to about
  107 bytes, and a Gentoo package build runs the tests in a `TMPDIR` of 43 characters or more.
  Under `--headless`, `shaodesk msg headless_output` and `headless_keyboard` plug in outputs and
  keyboards (`headless_keyboard key NAME CODE press` types on one; see `keymap_smoke.py`), and
  `wayland_probe --keymap` prints the keymap an application gets. A `wayland_probe` window with
  `SHAODESK_PROBE_DRAG=source` drags a line of text on a button press of `pointer_probe`'s, and
  one with `=target` takes it, each printing what it hears; `get seat` says where the drag is
  (see `drag_focus_smoke.py`).

## A fast loop

```sh
cmake --build build --target shaodesk          # only the compositor
ctest --test-dir build -j8 -R sticky           # only the tests you are working on
ctest --test-dir build -j8                     # everything, about 25 seconds
tools/check-all.sh                             # plus the sanitizer build, before merging
```

`build/compile_commands.json` is there for clangd. `shaodesk --headless` with
`WLR_RENDERER=pixman` runs the compositor with no display, as the tests do.
