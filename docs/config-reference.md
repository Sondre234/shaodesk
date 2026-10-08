<!-- Generated from src/config_schema.cpp by tests/config_diagnostics_tests.cpp. Do not edit by
     hand: change the schema, then run `SHAODESK_UPDATE_DOCS=1 ctest -R config`. -->
# Configuration reference

shaodesk reads a Lua file that returns a table (`config/init.lua` is a commented example). Every setting is optional. An unknown setting, a value of the wrong type, or a value out of range is an error naming the file and line, with a suggestion for a misspelled name; `shaodesk --check-config` reports it. The running desktop reloads the file when it is saved; one with an error loads the default configuration instead and shows the error across the top of the screen until the file is fixed.

Ranges are inclusive. Colors are `"#RRGGBB"` (with `#RRGGBBAA` where noted).

## General

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `version` | integer | 1 | - | Configuration API version; only 1 exists. |
| `extends` | string | unset | - | `"default"` layers this file over the shipped default configuration: it supplies every setting and binding this file omits, and a binding with `action = "none"` removes a default one. |
| `theme` | string | unset | - | A Lua file, relative to this one, that supplies every setting this file omits; `shaodesk import` writes one. A missing file is ignored. |
| `profile` | string | unset | - | The appearance profile to start with, a name from `profiles`. One picked from the panel's menu, the command palette or `shaodesk msg profile NAME` replaces it until another is picked; that choice is kept in `$XDG_STATE_HOME/shaodesk/profile`. |
| `profiles` | table of tables | unset | - | Appearance profiles, keyed by a name of up to 32 letters, digits, `-` and `_` (not `next` or `prev`), at most 32. Each holds `appearance`, `windows` and `shell` settings that replace this file's own (and its theme's and defaults') while the profile is in use, e.g. `light = { shell = { panel_color = "#f2f4f8" } }`. Every profile is checked as the file loads. A file with `extends = "default"` gets the default configuration's profiles only when it has none of its own. |
| `xwayland` | boolean | true | - | Run X11 applications; Xwayland starts on first use. Restart to change. |
| `auto_reload` | boolean | true | - | Reload when this file, or another `.lua` or `.xkb` file in its directory (or the `keyboard.file` there), is saved. A file with an error loads the default configuration instead and shows the error on screen. |
| `terminal` | list of strings | unset | - | The terminal the `terminal` action (Super + Q) opens, as a program and its arguments. Unset, it is `$TERMINAL` when that program is installed, else the first of kitty, foot, alacritty, wezterm, ghostty, konsole, gnome-terminal and xterm found on `PATH`. |

## `appearance`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `appearance` | table | - | - | Desktop look. |
| `appearance.background` | color | "#19212e" | - | Background color behind everything, `#RRGGBB`. |

## `keyboard`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `keyboard` | table | - | - | Keyboard layout and key repeat. |
| `keyboard.layout` | string | "us" | - | XKB layout name, e.g. `"us"` or `"no"`. |
| `keyboard.variant` | string | "" | - | XKB layout variant, e.g. `"intl"`. |
| `keyboard.model` | string | "" | - | XKB keyboard model. |
| `keyboard.options` | string | "" | - | XKB options, e.g. `"caps:escape"`. |
| `keyboard.rules` | string | "" | - | XKB rules the other names are looked up in; `""` uses xkbcommon's default, `evdev`. |
| `keyboard.file` | string | "" | - | An XKB keymap file, such as `xkbcli compile-keymap` writes or a hand-written `xkb_keymap { ... }`, used instead of the names above: absolute, starting with `~/`, or relative to this file. One that cannot be read or compiled is a configuration error; should it break while the session runs, the names above stand in for it. |
| `keyboard.repeat_rate` | integer | 25 | 0 to 100 | Key repeats per second. |
| `keyboard.repeat_delay` | integer | 600 | 0 to 5000 | Milliseconds a key is held before it repeats. |

## `mouse`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `mouse` | table | - | - | Pointer settings. |
| `mouse.modifier` | enum | "Alt" | - | Modifier for window drags: modifier + left drag moves, right drag resizes. One of `Alt`, `Super`, `Ctrl`, `Shift`. |
| `mouse.speed` | number | unset | -1 to 1 | Pointer speed in standalone sessions; unset keeps the device default. |
| `mouse.acceleration` | enum | unset | - | `"flat"` or `"adaptive"`; unset keeps the device default. |
| `mouse.natural_scroll` | boolean | unset | - | Natural scrolling for mice; unset keeps the device default. |
| `mouse.focus_follows` | boolean | true | - | Hovering a window focuses it, without raising it. |

## `touchpad`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `touchpad` | table | - | - | Touchpad settings (standalone sessions). |
| `touchpad.natural_scroll` | boolean | unset | - | Natural scrolling; unset keeps the device default. |
| `touchpad.tap_to_click` | boolean | unset | - | Tap to click; unset keeps the device default. |
| `touchpad.disable_while_typing` | boolean | unset | - | Ignore the touchpad while typing; unset keeps the device default. |

## `layout`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `layout` | table | - | - | Gaps, workspaces, and tiling. |
| `layout.gap` | integer | 8 | 0 to 100 | Pixels between windows and at the screen edges; `gap_inner` and `gap_outer` override it. |
| `layout.gap_inner` | integer | `gap` | 0 to 100 | Pixels between tiled windows. |
| `layout.gap_outer` | integer | `gap` | 0 to 100 | Pixels between tiles and the edges. |
| `layout.smart_gaps` | boolean | true | - | No gaps around a tiled window that is alone on its workspace. |
| `layout.workspaces` | integer | 4 | 1 to 10 | Workspaces per monitor. |
| `layout.workspace_names` | list of strings | unset | - | Names for workspaces 1, 2, ...; at most `workspaces` of them, each up to 32 characters, not a number and not repeated ("" leaves one unnamed). The panel shows them, and `workspace` and `move_to_workspace` (bindings with `workspace = "web"`, or `shaodesk msg workspace web`) accept them. |
| `layout.tiling` | boolean | false | - | Start every monitor with automatic tiling; `outputs.monitors.<name>.tiling` overrides it. |
| `layout.tiling_per_workspace` | boolean | false | - | Toggling tiling (`toggle_tiling`, the panel button) turns it on or off for the current workspace only, instead of every workspace of the monitor; workspaces not toggled follow the monitor's setting. |
| `layout.tile_layout` | enum | "dwindle" | - | Tiling layout: `"dwindle"`, `"master"`, `"spiral"`, `"monocle"`, or `"scroll"`. |
| `layout.master_ratio` | number | 0.55 | 0.1 to 0.9 | Share of the screen the master area takes in the master layout. |
| `layout.master_count` | integer | 1 | 1 to 8 | Windows in the master area. |
| `layout.outputs` | table of tables | unset | - | Layout defaults per monitor, keyed by connector name or by `"desc:"` and the start of "make model serial" as in `outputs.monitors`, e.g. `["DP-1"] = { tile_layout = "scroll" }`. They apply to the workspaces of that monitor that were not set by hand, and follow a reload and a monitor's return. |
| `layout.outputs.<name>.tile_layout` | enum | `layout.tile_layout` | - | This monitor's tiling layout: `"dwindle"`, `"master"`, `"spiral"`, `"monocle"`, or `"scroll"`. |
| `layout.outputs.<name>.master_ratio` | number | `layout.master_ratio` | 0.1 to 0.9 | This monitor's master ratio. |
| `layout.outputs.<name>.master_count` | integer | `layout.master_count` | 1 to 8 | This monitor's master count. |
| `layout.scroll` | table | - | - | The scrolling layout: columns on an endless strip, the screen a view onto it. |
| `layout.scroll.follow` | enum | "center" | - | How the view follows focus: `"center"` keeps the focused column centered, `"edge"` moves only as far as needed to show it, `"never"` moves only for scroll actions, new windows and `center_column`. |
| `layout.scroll.width` | number | 0.5 | 0.1 to 1 | Width of a new column, as a share of the screen. |
| `layout.scroll.step` | number | 0.1 | 0.01 to 0.5 | What `column_widen` and `column_narrow` add to or take from a column's width. |
| `layout.scroll.presets` | list of numbers | { 1/3, 1/2, 2/3, 1 } | - | Widths `column_cycle_width` steps through, each 0.1 to 1; at most 8. |

## `outputs`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `outputs` | table | - | - | Monitor arrangement. |
| `outputs.order` | list of strings | unset | - | Connector names from left to right; unlisted monitors follow on the right. At most 8, without duplicates. |
| `outputs.primary` | string | leftmost | - | Connector where the cursor starts. |
| `outputs.return_windows` | boolean | true | - | When a monitor is unplugged its windows move to the nearest one, keeping their workspace numbers; with this on they go back, to the same workspace and place, when it returns. |
| `outputs.monitors` | table of tables | unset | - | Per-monitor settings, keyed by connector name or by `"desc:"` and the start of "make model serial", e.g. `["DP-3"] = { mode = "2560x1440@144" }`. |
| `outputs.monitors.<name>.enabled` | boolean | true | - | `false` turns the monitor off. |
| `outputs.monitors.<name>.mode` | string | preferred | - | `"WIDTHxHEIGHT"` or `"WIDTHxHEIGHT@HZ"`, e.g. `"2560x1440@144"`. |
| `outputs.monitors.<name>.scale` | number | auto | 0.25 to 10 | Output scale factor. |
| `outputs.monitors.<name>.position` | table | auto | - | `{ x = N, y = N }` in layout pixels; both are required. |
| `outputs.monitors.<name>.position.x` | integer | - | -65536 to 65536 | Horizontal position. |
| `outputs.monitors.<name>.position.y` | integer | - | -65536 to 65536 | Vertical position. |
| `outputs.monitors.<name>.transform` | integer | 0 | 0 to 7 | Rotation and flip, as `wl_output.transform`: 0 normal, 1-3 rotated 90/180/270, 4-7 the same flipped. |
| `outputs.monitors.<name>.vrr` | boolean | false | - | Variable refresh rate. |
| `outputs.monitors.<name>.tiling` | boolean | unset | - | Automatic tiling on this monitor; unset follows `layout.tiling`. |

## `windows`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `windows` | table | - | - | Window borders, opacity, and rules. |
| `windows.border_width` | integer | 0 | 0 to 20 | Border in pixels around each window; tiles shrink to keep it in their slot. |
| `windows.corner_radius` | integer | 10 | 0 to 40 | Radius in pixels of the corners of windows (which ones, `round` says) and of their border; 0 keeps them square. Needs wlroots built with shaodesk's rounded-corners patch. |
| `windows.round` | enum | "tiling" | - | Which windows get rounded corners: `"tiling"`, those on a monitor with tiling on, floating ones included; `"always"`, every window, but for one on a monitor without tiling that draws a shadow of its own around it (a client-side frame, as GTK's), which keeps its own corners. Fullscreen and maximized windows stay square. |
| `windows.border_color` | color | "#7da8ff" | - | Focused window border, `#RRGGBB` or `#RRGGBBAA`. |
| `windows.border_inactive_color` | color | "#404a5c" | - | Other windows' border, `#RRGGBB` or `#RRGGBBAA`. |
| `windows.opacity` | number | 1.0 | 0.05 to 1 | Focused window opacity. |
| `windows.inactive_opacity` | number | `opacity` | 0.05 to 1 | Unfocused window opacity. |
| `windows.dim_inactive` | number | 0 | 0 to 0.9 | How much darker windows without focus are, 0 for not at all: black laid over them, so it works for every application and tints nothing. Fades over `dim_duration`. |
| `windows.dim_duration` | integer | 180 | 0 to 2000 | Milliseconds the dimming takes to fade in and out; 0, or `animations.enabled = false`, switches it at once. |
| `windows.activation` | string | "urgent" | - | What an unfocused application asking for attention (xdg-activation, an X11 urgency hint) gets: `"urgent"` marks the window (border, taskbar, workspace indicator) and leaves focus alone, `"focus"` focuses it and switches to its workspace, `"ignore"` drops the request. `focus_urgent` jumps to the oldest urgent window. |
| `windows.urgent_color` | color | "#ff9e64" | - | Border of an urgent window, `#RRGGBB` or `#RRGGBBAA`; it pulses for a few seconds, also on windows without a `border_width`. |
| `windows.swallow` | table | - | - | Window swallowing: a window started from a terminal takes its place and hides it while it lives; closing the window brings the terminal back. Matched by process ancestry, so the application has to be a descendant of the terminal's process. `swallow_toggle` does it by hand. |
| `windows.swallow.enabled` | boolean | false | - | Swallow automatically as windows open; `swallow_toggle` works either way. |
| `windows.swallow.terminals` | list of strings | kitty, foot, footclient, Alacritty, org.wezfurlong.wezterm, com.mitchellh.ghostty, xterm, URxvt, org.kde.konsole, org.gnome.Terminal | - | The `app_id`s (X11 class) of terminals that can be swallowed, compared without regard to case; at most 32. A window that is itself one of these never swallows. |
| `windows.swallow.exceptions` | list of strings | unset | - | `app_id`s that never swallow their terminal, at most 32. |
| `windows.magnet` | table | - | - | Magnetic edges: while a floating window is dragged or resized, its edges stick to the edges of the screen, of the area panels leave free, and of other windows nearby, and a guide line shows which one holds it. Tiles, and drags that will retile the window, are not affected; half-screen snapping and dropping at the top edge work as before. |
| `windows.magnet.enabled` | boolean | true | - | Turns the magnetism off with `false`. |
| `windows.magnet.distance` | integer | 12 | 0 to 200 | How near, in pixels, an edge has to come to be caught; it stays held until the pointer has moved that far away again. |
| `windows.magnet.guides` | boolean | true | - | Draw a line along the edge that holds the window. |
| `windows.magnet.guide_color` | color | "#7da8ffb3" | - | Color of the guide line, `#RRGGBBAA`. |
| `windows.magnet.bypass` | enum | "Shift" | - | Holding this modifier while dragging turns the magnetism off: `Shift`, `Ctrl`, `Alt`, `Super`, or `none`. |
| `windows.placement` | enum | "cascade" | - | Where a new floating window opens (tiles go where the layout puts them, and rules with a `position` win): `"cascade"` steps each one 32 pixels down and right, `"center"` opens it in the middle of the screen's free area, `"smart"` where it covers the other windows least, centered in the largest gap that holds it, and cascading when nothing is free. |
| `windows.drag_strip` | integer | 6 | 0 to 100 | Pixels along the top of a window without a title bar (kitty, X11 applications) that move it when dragged, as a title bar would; they no longer reach the application. 0 turns this off. |
| `windows.controls` | enum | "flat" | - | How the controls of the windows shaodesk decorates look; they show while the pointer is near their corner. `"flat"`: minimize, fullscreen and close buttons on a dark strip at the top-right. `"traffic_lights"`: close, minimize and fullscreen as red, yellow and green circles at the top-left, grey while the window has no focus, their symbols shown while the pointer is on one. |
| `windows.shadow` | table | - | - | A soft shadow under each window that draws none of its own (a client-side frame with a shadow keeps that one), following its rounded corners; none under fullscreen and maximized windows. It takes no input and is no part of the window's size or place. |
| `windows.shadow.enabled` | boolean | false | - | Draw the shadows. |
| `windows.shadow.color` | color | "#00000059" | - | The shadow under the focused window where it is darkest, `#RRGGBBAA`. |
| `windows.shadow.inactive_color` | color | "#00000033" | - | The shadow under other windows, `#RRGGBBAA`. |
| `windows.shadow.blur` | integer | 30 | 0 to 100 | How soft the shadow's edge is: it fades out over about this many pixels, as CSS's blur radius; 0 for a hard edge. |
| `windows.shadow.offset` | integer or table | 10 | -50 to 50 | How far the shadow falls below the window in pixels, or `{ x, y }` to move it sideways too (right and down). |
| `windows.buttons` | string | "appmenu:minimize,maximize,close" | - | GTK button layout for windows that draw their own frame; lowercase letters, `_`, `,` and `:` only. Restart to change. |
| `windows.rules` | list of tables | unset | - | Per-application rules, at most 256. Opacity comes from the first matching rule that sets it; actions apply as a window opens, every matching rule in order, later ones winning. A rule needs `app_id` or `title`. |
| `windows.rules[].app_id` | regex | any | - | ECMAScript regular expression searched in the application ID (X11: the class). |
| `windows.rules[].title` | regex | any | - | ECMAScript regular expression searched in the window title. |
| `windows.rules[].opacity` | number | - | 0.05 to 1 | Opacity of a matching focused window. |
| `windows.rules[].inactive_opacity` | number | `opacity` | 0.05 to 1 | Opacity of a matching unfocused window. |
| `windows.rules[].floating` | boolean | unset | - | Open floating or tiled. |
| `windows.rules[].workspace` | integer | unset | 1 to 10 | Open on this workspace of its monitor, without switching there (at most `layout.workspaces`). |
| `windows.rules[].output` | string | unset | - | Open on this monitor: a connector, or `"desc:"` and the start of its description. |
| `windows.rules[].size` | list or table | unset | - | `{ width, height }` or `{ width = W, height = H }`, 1 to 16384. |
| `windows.rules[].position` | string or table | unset | - | `"center"`, or `{ x, y }` / `{ x = X, y = Y }` from the top-left of the monitor's usable area, -16384 to 16384. |
| `windows.rules[].fullscreen` | boolean | unset | - | Open fullscreen. |
| `windows.rules[].maximize` | boolean | unset | - | Open maximized. |
| `windows.rules[].focus` | boolean | unset | - | `false` opens the window without focusing it. |
| `windows.rules[].sticky` | boolean | unset | - | Show on every workspace of its monitor. |

## `peek`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `peek` | table | - | - | Peek: `peek` (hold a key) and `peek_toggle` make every window nearly transparent to show the desktop, with borders and the panel left alone. Resting on a window's picture on the taskbar's card fades every other window the same way. |
| `peek.opacity` | number | 0.12 | 0 to 0.9 | Opacity windows fade to while peeking; 0 hides them completely. |
| `peek.duration` | integer | 150 | 0 to 2000 | Milliseconds of the fade in and out; 0, or `animations.enabled = false`, switches at once. |

## `night_light`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `night_light` | table | - | - | Night light: the screen turns warm after sunset, easing over `transition` minutes. The actions `night_light_toggle`, `night_light_on`, `night_light_off` and `night_light_auto` override the schedule. |
| `night_light.enabled` | boolean | false | - | Follow the schedule below. |
| `night_light.day_temperature` | integer | 6500 | 1000 to 10000 | Colour temperature in kelvin by day; 6500 leaves colours alone. |
| `night_light.night_temperature` | integer | 3400 | 1000 to 10000 | Colour temperature in kelvin at night; lower is warmer. |
| `night_light.sunrise` | string | "07:00" | - | Time the screen turns cool again, as "HH:MM". Written out together with `sunset` it wins over the location. |
| `night_light.sunset` | string | "20:00" | - | Time the screen turns warm, as "HH:MM". |
| `night_light.latitude` | number | unset | -90 to 90 | Degrees north; with `longitude` (set both) it gives sunrise and sunset for each day. |
| `night_light.longitude` | number | unset | -180 to 180 | Degrees east. |
| `night_light.transition` | number | 30 | 0 to 240 | Minutes over which the temperature changes, centred on sunrise and sunset. |

## `hot_corners`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `hot_corners` | table | - | - | Hot corners: pushing the pointer into a screen corner and leaving it there runs a request, written as for `shaodesk msg` (an action name and its argument, or `spawn PROGRAM ARGS`). Nothing runs over a fullscreen window or while a button is held. |
| `hot_corners.size` | integer | 2 | 1 to 64 | Side of the corner square in pixels. |
| `hot_corners.delay` | integer | 150 | 0 to 5000 | Milliseconds the pointer must stay in the corner; it must leave before the corner can run again. |
| `hot_corners.top_left` | string | unset | - | What the top-left corner runs, for instance `toggle_overview` or `workspace 2`. |
| `hot_corners.top_right` | string | unset | - | What the top-right corner runs. |
| `hot_corners.bottom_left` | string | unset | - | What the bottom-left corner runs. |
| `hot_corners.bottom_right` | string | unset | - | What the bottom-right corner runs. |

## `zoom`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `zoom` | table | - | - | Screen magnifier: `zoom_in` and `zoom_out` change the magnification by one step, `zoom_reset` returns to 1x. The magnified view follows the pointer, which stays over what it clicks. |
| `zoom.step` | number | 1.25 | 1.05 to 4 | Factor of one `zoom_in` or `zoom_out` step. |
| `zoom.max` | number | 8 | 1.5 to 32 | Largest magnification. |
| `zoom.duration` | integer | 150 | 0 to 2000 | Milliseconds a zoom change takes; 0, or `animations.enabled = false`, switches at once. |
| `zoom.scroll_modifier` | string | "" | - | Holding this modifier turns the scroll wheel into zoom steps (up zooms in); "" leaves the wheel alone. One of Alt, Super, Ctrl, Shift. |

## `animations`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `animations` | table | - | - | Window and shell animations. |
| `animations.enabled` | boolean | true | - | Windows fade in and out and tiles glide into place; `false` also stills the shell's animations. |
| `animations.duration` | integer | 120 | 10 to 1000 | Animation length in milliseconds. |
| `animations.speed` | number | 1.0 | 0.1 to 10 | Speed multiplier for every animation, the shell's too: 2 makes them twice as fast, 0.5 half as fast. |
| `animations.curve` | enum | per kind | - | Easing for every animation that does not set its own: `"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"`, `"ease-out-quint"`, `"overshoot"`, `"spring"`, or `"bezier(x1, y1, x2, y2)"` (x between 0 and 1, y between -2 and 3). Unset, moves use `"spring"`, workspace slides `"ease-out-quint"`, and the rest `"ease-out"`. |
| `animations.late_frame_ms` | integer | 80 | 0 to 1000 | A frame later than this finishes running animations instead of stuttering through them; 0 never skips. |
| `animations.open` | table | - | - | A window appearing. Overrides the animation-wide settings. |
| `animations.open.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.open.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |
| `animations.close` | table | - | - | A window going. Overrides the animation-wide settings. |
| `animations.close.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.close.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |
| `animations.move` | table | - | - | A tile or window moving to a new place. Overrides the animation-wide settings. |
| `animations.move.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.move.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |
| `animations.workspace` | table | - | - | The view sliding to another workspace. Overrides the animation-wide settings. |
| `animations.workspace.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.workspace.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |
| `animations.workspace.distance` | number | 0.08 | 0 to 1 | How far windows slide when the workspace changes, as a share of the monitor's width; they fade as they go. 0 only fades. Large values slide over neighbouring monitors. |
| `animations.fullscreen` | table | - | - | A window entering or leaving fullscreen. Overrides the animation-wide settings. |
| `animations.fullscreen.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.fullscreen.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |
| `animations.focus` | table | - | - | Border and opacity changing with focus. Overrides the animation-wide settings. |
| `animations.focus.duration` | integer | `duration` | 0 to 1000 | Length in milliseconds; 0 turns this animation off. |
| `animations.focus.curve` | enum | `curve` | - | Easing curve, as `animations.curve`. |

## `overview`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `overview` | table | - | - | The overview: every window of a monitor's workspace as a live thumbnail, opened by the `toggle_overview` action. |
| `overview.enabled` | boolean | true | - | The overview actions work; `false` turns them off. |
| `overview.gap` | integer | 24 | 0 to 200 | Pixels between thumbnails. |
| `overview.animation` | boolean | true | - | Windows glide between their places and the grid as it opens and closes. |
| `overview.duration` | integer | 180 | 10 to 1000 | Milliseconds the opening and closing glide takes. |
| `overview.strip` | boolean | true | - | A strip of the monitor's workspaces above the grid, to switch to or to drop a window on. |
| `overview.hot_corner` | enum | "none" | - | Pushing the pointer into this screen corner opens the overview: `"none"`, `"top-left"`, `"top-right"`, `"bottom-left"`, or `"bottom-right"`. |
| `overview.dim` | number | 0.86 | 0 to 1 | How opaque the backdrop behind the thumbnails is, from 0 (clear) to 1. |

## `bindings`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `bindings` | list of tables | unset | - | Shortcuts, at most 512, checked in order. A binding has a `key` or a `button`, an `action`, and optionally `mods`. Keyboard bindings must not repeat a key and modifier combination. |
| `bindings[].mods` | list of strings | {} | - | Up to four of `Alt`, `Super`, `Ctrl`, `Shift`, without repeats. |
| `bindings[].key` | string | - | - | An XKB keysym name, e.g. `"q"`, `"Left"`, `"Print"`, `"minus"`. Not with `button`. |
| `bindings[].button` | enum | - | - | A mouse button: `left`, `right`, `middle`, `side`, `extra`, `forward`, `back`. Not with `key`. |
| `bindings[].app_id` | regex | any | - | Only with `button`: act only over windows whose app ID matches. |
| `bindings[].desktop` | boolean | false | - | Only with `button`: also act over the bare desktop. |
| `bindings[].action` | enum | - | - | What the binding does; see the action list below. `"none"` unbinds (with `extends`) or hands a button click to the application. |
| `bindings[].command` | list of strings | - | - | With `spawn` only: the program and its arguments, never a shell string. |
| `bindings[].workspace` | integer | - | 1 to 10 | With `workspace` and `move_to_workspace` only: a number up to `layout.workspaces`, or a name from `layout.workspace_names`. |
| `bindings[].output` | string | "next" for `swap_workspaces` | - | With `move_workspace_to_output` (required) and `swap_workspaces`: the other monitor, `"left"` or `"right"` of the focused one, `"next"` or `"prev"` in order with wrap, or a connector name or `"desc:"` description as in `outputs.monitors`. |
| `bindings[].mode` | enum | "region" | - | With `screenshot` only: `"region"`, `"output"`, or `"window"`. |
| `bindings[].amount` | integer | 40 | 1 to 4000 | With `resize_*` only: pixels moved per press. |
| `bindings[].layout` | string or integer | "next" | - | With `switch_layout` only: `"next"` or `"prev"` (wrapping), or a layout's number from 1, in the order of `keyboard.layout`. |

## `notifications`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `notifications` | table | - | - | Notifications: the shell serves `org.freedesktop.Notifications` on the session bus and shows each one as a card on the focused monitor; clicking a card runs its default action, hovering pauses its timer, and the clock's flyout keeps a history. Critical notifications stay until dismissed. The actions `dnd_toggle`, `dnd_on` and `dnd_off` (or `shaodesk msg dnd toggle`) silence the cards. |
| `notifications.enabled` | boolean | true | - | Serve notifications. Turn it off to run another daemon such as mako or dunst. |
| `notifications.position` | enum | "top-right" | - | Corner of the focused monitor the cards stack from: `"top-right"`, `"top-left"`, `"bottom-right"` or `"bottom-left"`. |
| `notifications.timeout` | integer | 6000 | 0 to 600000 | Milliseconds before a card goes when the application asked for the default; 0 keeps cards until dismissed. An application's own timeout wins, and critical ones never expire. |
| `notifications.max_visible` | integer | 4 | 1 to 10 | Cards shown at once; newer ones push the oldest into the history. |
| `notifications.dnd` | boolean | false | - | Start in do-not-disturb: no cards, but everything still reaches the history. |
| `notifications.width` | integer | 360 | 200 to 800 | Card width in pixels. |
| `notifications.history` | integer | 100 | 0 to 1000 | Notifications the clock's flyout keeps; 0 keeps none. |

## `osd`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `osd` | table | - | - | On-screen display: a small pill near the bottom centre of the focused monitor that shows a label and a level for volume and brightness changes and for `shaodesk msg osd TEXT [PERCENT]`, then fades out. |
| `osd.enabled` | boolean | true | - | Show the display. |
| `osd.position` | enum | "bottom" | - | `"bottom"` or `"top"` edge of the monitor. |
| `osd.timeout` | integer | 1500 | 200 to 10000 | Milliseconds the display stays before it fades out. |
| `osd.volume` | boolean | true | - | Show it when the default output's volume or mute changes, by keys or from any application. |
| `osd.brightness` | boolean | true | - | Show it when a backlight's brightness changes. |

## `power`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `power` | table | - | - | Power controls: the `lock`, `suspend`, `hibernate`, `reboot`, `poweroff` and `logout` actions. All but `lock` and `logout` go through logind (systemd-logind or elogind). |
| `power.lock_command` | list of strings | { "swaylock", "-f" } | - | The screen locker `lock` starts, an `ext-session-lock-v1` client, as a program and its arguments; `{}` for none. Locking is offered only while the program is installed. |
| `power.lock_before_sleep` | boolean | true | - | Lock the screen with `lock_command` before the machine sleeps, whether `suspend`, `hibernate`, the lid or an idle daemon sends it to sleep, and hold the sleep until the lock holds. |
| `power.close_windows` | boolean | true | - | `poweroff`, `reboot` and `logout` first ask every window to close, as its close button would, and go ahead once all have, so that applications save their state. |
| `power.close_timeout` | integer | 5000 | 500 to 60000 | Milliseconds the windows get to close. One still open then (an application asking whether to save) cancels the power off, unless `force` is set; answering it in time lets the power off go on. |
| `power.countdown` | integer | 10 | 0 to 300 | Seconds the shell's confirmation of power off, restart and log out counts down before it goes ahead (Escape, Cancel or a click beside it gives up); 0 waits for a click. |
| `power.force` | boolean | false | - | Go ahead with the power off, reboot or log out even when windows are still open after `close_timeout`; their applications are then ended without saving. |

## `startup`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `startup` | list of commands | unset | - | Commands started once when the compositor starts (not on reload), at most 32; each is an argument list, e.g. `{ { "kitty" } }`. |

## `shell`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `shell` | table | - | - | The desktop shell: panel, wallpaper, launchers. |
| `shell.enabled` | boolean | true | - | Start the shell with the compositor. |
| `shell.style` | enum | "taskbar" | - | How the shell is laid out: `"taskbar"` (one bar with a start menu, as on Windows) or `"macos"` (a menu bar along the top and a dock at the bottom, the panel settings applying to the dock). |
| `shell.panel_height` | integer | 52 | 24 to 100 | Panel height in pixels. |
| `shell.panel_position` | enum | "bottom" | - | `"top"` or `"bottom"`. |
| `shell.panel_margin` | integer or table | 0 | - | Pixels around the panel, 0 to 200: one number, or `{ top, right, bottom, left }` by name, to make it float. |
| `shell.panel_margin.top` | integer | 0 | 0 to 200 | Top margin. |
| `shell.panel_margin.right` | integer | 0 | 0 to 200 | Right margin. |
| `shell.panel_margin.bottom` | integer | 0 | 0 to 200 | Bottom margin. |
| `shell.panel_margin.left` | integer | 0 | 0 to 200 | Left margin. |
| `shell.panel_radius` | integer | 0 | 0 to 50 | Panel corner radius. |
| `shell.font` | string | "" | - | Font family; empty keeps the Qt default. |
| `shell.font_size` | integer | 12 | 6 to 48 | Taskbar text size in pixels. |
| `shell.renderer` | enum | "gpu" | - | How the shell draws: `"gpu"` (Qt Quick's OpenGL or Vulkan renderer, which effects such as shadows need) or `"software"` (the CPU, without effects, but it starts about three times faster and uses about half the memory, for a weak machine). Read at shell start, so a change needs a restart. |
| `shell.icons_only` | boolean | true | - | Taskbar buttons show only the window icon, the title as a tooltip. |
| `shell.group_windows` | boolean | true | - | One taskbar button per application; `false` gives every window its own. |
| `shell.thumbnails` | table | - | - | Pictures of the windows on the taskbar and in the window switcher: resting the pointer on a button with windows shows a card beside it with a small picture of each, and Alt + Tab shows each window as a card with its picture, as Windows does. The taskbar style only; the macOS style's dock lists a stack's windows by name, and its switcher shows icons. |
| `shell.thumbnails.enabled` | boolean | true | - | Show the pictures; `false` gives a window's button its title as a tooltip again, a stack the list of its windows' titles, and the switcher its grid of icons. |
| `shell.thumbnails.delay` | integer | 400 | 0 to 2000 | Milliseconds the pointer rests on the button before the card opens. |
| `shell.thumbnails.size` | integer | 240 | 120 to 480 | Width of one picture in pixels, which is 5/8 as tall. With more windows than fit across the monitor they get narrower, down to 60 % of it, and past that the card lists the windows by title instead. The switcher's pictures are as tall, each as wide as its window's proportions make it, and get smaller, down to 60 %, when there are many. |
| `shell.thumbnails.live` | boolean | true | - | The pictures follow their windows while the card or the switcher is open, at most 30 times a second; `false` takes one of each window just before the card opens, or as the switcher opens. |
| `shell.search` | table | - | - | What the command palette's and the start menu's search find besides applications, windows, workspaces and actions. |
| `shell.search.files` | boolean | true | - | Find files and folders by name: those used lately (`recently-used.xbel`), and those in `directories`, read in the background the first time a search asks and again once they are five minutes old or something changed. A leading `/` searches only files. |
| `shell.search.directories` | list of strings | unset | - | The folders whose files are found, at most 32, absolute or starting with `~/`; unset reads the XDG user folders (Documents, Downloads, ...) and the home folder. Hidden files and folders, other file systems, and what is inside a version-controlled tree (`.git`), a build tree (`CMakeCache.txt`, `CACHEDIR.TAG`), `node_modules` or `__pycache__` are left out. |
| `shell.search.depth` | integer | 4 | 1 to 10 | How many levels of folders below each of `directories` are read. |
| `shell.search.max_files` | integer | 20000 | 100 to 200000 | How many names are read at most, the folders nearest the top first. |
| `shell.search.web` | string or false | "https://duckduckgo.com/?q=%s" | - | The search engine of the last result, Search the web for “…”, which opens the default browser at this http or https address, `%s` replaced by the words typed (URL-encoded); `false` leaves it out. |
| `shell.workspaces_shown` | integer | 0 | 0 to 10 | How many of the monitor's workspaces the workspace indicator shows, the current one in the middle: `3` shows it with the one before and the one after, or the first or last three at either end. 0 shows them all. Scrolling still reaches every workspace. |
| `shell.accent` | color | "#7da8ff" | - | Accent color, `#RRGGBB` or `#RRGGBBAA`. |
| `shell.panel_color` | color | "#151e2c" | - | Panel color; `#RRGGBBAA` makes it translucent. |
| `shell.text_color` | color | "#edf2fa" | - | Panel text color, `#RRGGBB` or `#RRGGBBAA`. |
| `shell.wallpaper` | string | "" | - | Image path, absolute or relative to the configuration file. |
| `shell.wallpapers` | string | unset | - | The wallpaper picker's folder, searched with its subfolders: absolute, or starting with `~/`; unset uses `$XDG_PICTURES_DIR/wallpapers`, else `$XDG_PICTURES_DIR`. |
| `shell.widgets` | table | - | - | Panel widgets; each is switched off with `false`. One that can move takes `"bar"` to sit on the bar or `"quick"` to sit in the Quick Settings flyout instead, and `true` puts it in its default place. |
| `shell.widgets.workspaces` | boolean | true | - | This monitor's workspace numbers. |
| `shell.widgets.battery` | boolean or string | true | - | Charge and state, only where a battery exists: `"bar"` or `"quick"` (along Quick Settings' foot). By default in Quick Settings. |
| `shell.widgets.network` | boolean or string | true | - | Connection state, only where a network interface exists: `"bar"` or `"quick"` (a tile, and an icon on the Quick Settings button only while the link is down). By default in Quick Settings. |
| `shell.widgets.volume` | boolean or string | true | - | The default output's volume, only with a sound server: `"bar"` or `"quick"` (a slider, with the outputs and the applications' volumes). By default in Quick Settings. |
| `shell.widgets.clock` | boolean | true | - | The clock. |
| `shell.widgets.calendar` | boolean | true | - | The month calendar in the clock's flyout. |
| `shell.widgets.tiling` | boolean or string | true | - | Tiling on or off for the monitor: `"bar"` (a button) or `"quick"` (a tile). By default on the bar. |
| `shell.widgets.profiles` | boolean or string | true | - | The appearance profile picker, which lists `profiles` to switch between; shown only when there are two or more: `"bar"` (a button) or `"quick"` (a tile). By default in Quick Settings. |
| `shell.widgets.wallpapers` | boolean or string | true | - | The wallpaper picker: thumbnails of the images in `shell.wallpapers`; the one picked replaces `shell.wallpaper` for the profile in use until the configured one changes. `"bar"` (a button) or `"quick"` (a tile that opens the same picker). By default on the bar. |
| `shell.widgets.keyboard_layout` | boolean | true | - | The active keyboard layout's short name, such as `us`; clicking it switches to the next. Shown only when the keymap has two or more layouts. |
| `shell.widgets.power` | boolean | true | - | The power button in the application menu's bottom-right corner: lock, suspend, hibernate, restart, power off and log out, as far as `power.lock_command` and logind allow them. |
| `shell.widgets.tray` | boolean | true | - | The system tray: applications' status icons (StatusNotifierItem), shown while there are any. `false` also leaves the tray's D-Bus names to another program. |
| `shell.widgets.notifications` | boolean or string | true | - | Do-not-disturb and the unread notifications: `"bar"` (a bell that opens them, with their count; a right-click toggles do-not-disturb) or `"quick"` (a do-not-disturb tile). The clock does both either way. By default in Quick Settings. |
| `shell.launchers` | list of tables | unset | - | Pinned commands for programs without a desktop file, at most 64. |
| `shell.launchers[].name` | string | - | - | Label, 1 to 128 bytes. Required. |
| `shell.launchers[].icon` | string | "application-x-executable" | - | Icon theme name. |
| `shell.launchers[].command` | list of strings | - | - | Program and arguments. Required. |

## `screenshots`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `screenshots` | table | - | - | Screenshot bindings' output. |
| `screenshots.directory` | string | unset | - | Absolute, or starting with `~/`; unset uses `$XDG_PICTURES_DIR/Screenshots`. |
| `screenshots.clipboard` | boolean | true | - | Also copy the image with `wl-copy`. |
| `screenshots.notify` | boolean | true | - | Announce the file with `notify-send`, when it is installed. |

## `features`

| Setting | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `features` | table | - | - | Optional behaviours, each a boolean. |
| `features.workspace_back_and_forth` | boolean | false | - | The `workspace` action for the workspace already shown switches back to the previous one. |
| `features.scratchpad` | boolean | true | - | The `move_to_scratchpad` and `scratchpad_show` actions. |
| `features.sticky` | boolean | true | - | The `toggle_sticky` action. |
| `features.keyboard_resize` | boolean | true | - | The `resize_left/right/up/down` actions. |
| `features.window_rules` | boolean | true | - | The actions of `windows.rules`; opacity rules apply either way. |
| `features.groups` | boolean | true | - | The `group_toggle`, `group_next`, `group_prev`, `ungroup` and `group_merge_*` actions; off dissolves every group. |
| `features.group_join_new` | boolean | true | - | A window that opens while a window group has focus joins that group as a new tab. |

## Binding actions

`spawn`, `terminal`, `quit`, `close`, `cycle`, `snap_left`, `snap_right`, `maximize`, `restore`, `tile`, `reload`, `fullscreen`, `workspace`, `move_to_workspace`, `workspace_next`, `workspace_prev`, `workspace_back`, `toggle_tiling`, `layout_next`, `layout_prev`, `layout_dwindle`, `layout_master`, `layout_spiral`, `layout_monocle`, `layout_scroll`, `promote`, `focus_next`, `focus_prev`, `swap_next`, `swap_prev`, `master_grow`, `master_shrink`, `master_more`, `master_less`, `peek`, `peek_toggle`, `night_light_toggle`, `night_light_on`, `night_light_off`, `night_light_auto`, `zoom_in`, `zoom_out`, `zoom_reset`, `move_workspace_to_output`, `swap_workspaces`, `swallow_toggle`, `switch_layout`, `dnd_toggle`, `dnd_on`, `dnd_off`, `notification_history`, `clipboard_history`, `poweroff`, `reboot`, `suspend`, `hibernate`, `logout`, `lock`, `power_menu`, `scroll_left`, `scroll_right`, `column_widen`, `column_narrow`, `column_cycle_width`, `consume_left`, `consume_right`, `expel`, `center_column`, `toggle_floating`, `launcher`, `taskbar_focus`, `focus_left`, `focus_right`, `focus_up`, `focus_down`, `screenshot`, `move_left`, `move_right`, `move_up`, `move_down`, `move_to_scratchpad`, `scratchpad_show`, `toggle_sticky`, `resize_left`, `resize_right`, `resize_up`, `resize_down`, `switcher`, `switcher_prev`, `switcher_confirm`, `switcher_cancel`, `focus_last`, `focus_urgent`, `group_toggle`, `group_next`, `group_prev`, `ungroup`, `group_merge_left`, `group_merge_right`, `group_merge_up`, `group_merge_down`, `palette`, `toggle_overview`, `overview_confirm`, `overview_cancel`

`spawn` needs `command`; `workspace` and `move_to_workspace` need `workspace`; `screenshot` takes `mode`; `resize_*` take `amount`.

Modifiers: `Alt`, `Super`, `Ctrl`, `Shift`.
