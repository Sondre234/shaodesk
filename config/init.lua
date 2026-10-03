-- SPDX-License-Identifier: GPL-3.0-or-later
-- shaoDe configuration, API version 1.
-- Launch commands are argument arrays, never shell strings.
-- Hyprland-style Super shortcuts. A nested session inside a host that grabs Super
-- (Hyprland, GNOME) never sees them; set mod = "Alt" there.
local mod = "Super"

return {
    version = 1,
    -- `shaode import ~/.config` writes theme.lua from Hyprland, Waybar, wallbash, or pywal
    -- files. It fills in whatever this file leaves out, so the look settings below are
    -- comments showing their defaults; set one here to override the import.
    theme = "theme.lua",
    -- appearance = { background = "#19212e" },
    -- keyboard = { layout = "us", options = "", repeat_rate = 25, repeat_delay = 600 },
    mouse = {
        modifier = mod, -- modifier + left drag moves; right drag resizes
        -- focus_follows = true, -- hovering a window focuses it (without raising it)
        -- Standalone sessions only; unset keeps each device's default:
        -- speed = 0.0 (-1 to 1), acceleration = "flat" or "adaptive", natural_scroll = false
    },
    -- touchpad = { natural_scroll = true, tap_to_click = true, disable_while_typing = true },
    layout = {
        -- gap = 8, -- sets both; or gap_inner (between windows) and gap_outer (at the edges)
        workspaces = 4,
        tiling = false, -- automatic tiling on every monitor; outputs.monitors can override it,
        -- and each monitor's panel button toggles it there
    },
    windows = {
        -- border_width = 0, -- drawn around each window; tiles shrink to keep it in their slot
        -- border_color = "#7da8ff", -- the focused window; #RRGGBBAA also works
        -- border_inactive_color = "#404a5c",
        -- opacity = 1.0,
        -- inactive_opacity = 1.0,
        -- Buttons of windows that draw their own (Firefox's tab strip, GTK apps), in GTK's
        -- button-layout format; "" keeps the desktop's setting. Applications started from
        -- shaoDe see it; restart to change:
        -- buttons = "appmenu:minimize,maximize,close",
        -- Per application, by app_id and/or title (regular expressions; X11 windows match
        -- their class as app_id). Opacity comes from the first match that sets it. Actions
        -- apply as a window opens, every matching rule in order, later ones winning:
        -- floating, workspace (on its monitor, without switching there), output (connector
        -- or "desc:..."), size = { w, h }, position = "center" or { x, y } (from the
        -- monitor's top-left, panels excluded), fullscreen, maximize, focus = false, sticky.
        -- rules = {
        --     { app_id = "^firefox$", opacity = 0.9, inactive_opacity = 0.85 },
        --     { app_id = "^org.pulseaudio.pavucontrol$", floating = true, size = { 800, 500 },
        --       position = "center" },
        --     { app_id = "^thunderbird$", workspace = 2, focus = false },
        -- },
    },
    -- Windows fade in and out and tiles glide into place, over `duration` milliseconds:
    -- animations = { enabled = true, duration = 120 },
    outputs = {
        -- Left to right by connector name; unlisted monitors follow on the right.
        order = { "HDMI-A-1", "DP-3", "DP-1" },
        primary = "DP-3", -- the cursor starts here; without it, the leftmost monitor
        -- Per-monitor mode, scale, position, rotation (0-7), vrr, tiling, or enabled = false,
        -- keyed by connector or by "desc:" and the start of "make model serial", e.g.:
        -- monitors = { ["DP-3"] = { mode = "2560x1440@200", scale = 1.25, tiling = true } },
    },
    screenshots = {
        -- Saved as Screenshot_<date>_<time>.png; "~/" means your home directory. Empty or unset:
        -- $XDG_PICTURES_DIR/Screenshots, else ~/Pictures/Screenshots.
        -- directory = "~/Pictures/Screenshots",
        clipboard = true, -- also copy the image (needs wl-copy)
        notify = true, -- announce it with notify-send, when that is installed
    },
    -- Optional behaviours; the values shown are the defaults.
    features = {
        -- Sway's scratchpad: move_to_scratchpad (Super+Shift+minus) and scratchpad_show
        -- (Super+minus); false turns them off, and a reload brings hidden windows back.
        scratchpad = true,
        -- toggle_sticky (Super+P) pins a window to every workspace of its monitor; false turns
        -- the action off and returns sticky windows to their monitor's current workspace.
        sticky = true,
        -- resize_left/right/up/down (Super+Ctrl+Shift+arrows); false turns them off.
        keyboard_resize = true,
        -- The actions of windows.rules (floating, workspace, output, size, ...); opacity rules
        -- apply either way.
        window_rules = true,
        -- As sway's workspace_auto_back_and_forth: the `workspace` action for the workspace
        -- already shown on a monitor switches it back to the one it showed before. The
        -- `workspace_back` action (Super+Tab) does that whether this is on or not.
        workspace_back_and_forth = false,
    },
    xwayland = true, -- run X11 applications; Xwayland starts on first use (restart to change)
    shell = {
        enabled = true,
        -- panel_height = 52,
        -- panel_position = "bottom", -- or "top"
        -- panel_margin = 0, -- or { top = 8, right = 12, bottom = 0, left = 12 } to float
        -- panel_radius = 0,
        -- font = "", -- family name; empty keeps the default
        -- font_size = 12,
        -- icons_only = true, -- taskbar buttons as small icons; false adds each window's title
        -- group_windows = true, -- one button per application, its windows listed on hover;
        --                          false gives every window its own button
        -- accent = "#7da8ff",
        -- panel_color = "#151e2c", -- #RRGGBBAA makes it translucent
        -- text_color = "#edf2fa",
        -- wallpaper = "", -- absolute path, or relative to this configuration file
        -- Pin applications from the launcher or a window's right-click menu. Launchers here are
        -- for commands without a desktop file; they stay pinned ahead of those, e.g.:
        -- launchers = { { name = "Home", icon = "user-home", command = { "xdg-open", "." } } },
    },
    startup = {}, -- e.g. { { "kitty" } }
    bindings = {
        -- Mouse buttons bind too: left, right, middle, side, extra (most mice's back and
        -- forward thumb buttons), forward, back. app_id (a regular expression) limits one to
        -- windows under the pointer, desktop = true to the bare desktop; elsewhere the click
        -- reaches the application, so a browser keeps its own back and forward:
        -- { button = "side", app_id = "^kitty$", desktop = true, action = "close" },
        -- { button = "extra", app_id = "^kitty$", desktop = true, action = "spawn",
        --   command = { "kitty" } },
        { mods = { mod }, key = "q", action = "spawn", command = { "kitty" } },
        { mods = { mod }, key = "r", action = "launcher" },
        { mods = { mod }, key = "c", action = "close" },
        { mods = { mod }, key = "m", action = "quit" },
        { mods = { mod }, key = "v", action = "toggle_floating" },
        { mods = { mod }, key = "f", action = "fullscreen" },
        { mods = { mod }, key = "t", action = "tile" },
        { mods = { mod }, key = "s", action = "toggle_tiling" },
        { mods = { mod }, key = "p", action = "toggle_sticky" }, -- show on every workspace
        -- The window switcher lists every window on every monitor and workspace, most recently
        -- used first, on the focused monitor; releasing Alt focuses the selected one. Tab and
        -- the arrows move, Return picks, Escape cancels. `cycle` raises the next window at once.
        { mods = { "Alt" }, key = "Tab", action = "switcher" },
        { mods = { "Alt", "Shift" }, key = "Tab", action = "switcher_prev" },
        { mods = { mod }, key = "Left", action = "focus_left" },
        { mods = { mod }, key = "Right", action = "focus_right" },
        { mods = { mod }, key = "Up", action = "focus_up" },
        { mods = { mod }, key = "Down", action = "focus_down" },
        { mods = { mod, "Shift" }, key = "Left", action = "move_left" },
        { mods = { mod, "Shift" }, key = "Right", action = "move_right" },
        { mods = { mod, "Shift" }, key = "Up", action = "move_up" },
        { mods = { mod, "Shift" }, key = "Down", action = "move_down" },
        -- Resize by 40 pixels, or by `amount = N` added to a binding. A tile moves its split on
        -- that side that way (growing), or the one on its other side when it touches the screen
        -- edge (shrinking); a floating window moves its right or bottom edge that way.
        { mods = { mod, "Ctrl", "Shift" }, key = "Left", action = "resize_left" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Right", action = "resize_right" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Up", action = "resize_up" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Down", action = "resize_down" },
        -- Also available: snap_left, snap_right (half the screen), maximize, restore.
        -- Lock with any ext-session-lock client, e.g.:
        -- { mods = { mod }, key = "l", action = "spawn", command = { "swaylock" } },
        { mods = { mod }, key = "1", action = "workspace", workspace = 1 },
        { mods = { mod }, key = "2", action = "workspace", workspace = 2 },
        { mods = { mod }, key = "3", action = "workspace", workspace = 3 },
        { mods = { mod }, key = "4", action = "workspace", workspace = 4 },
        { mods = { mod, "Shift" }, key = "1", action = "move_to_workspace", workspace = 1 },
        { mods = { mod, "Shift" }, key = "2", action = "move_to_workspace", workspace = 2 },
        { mods = { mod, "Shift" }, key = "3", action = "move_to_workspace", workspace = 3 },
        { mods = { mod, "Shift" }, key = "4", action = "move_to_workspace", workspace = 4 },
        { mods = { "Ctrl", mod }, key = "Right", action = "workspace_next" },
        { mods = { "Ctrl", mod }, key = "Left", action = "workspace_prev" },
        { mods = { mod }, key = "Tab", action = "workspace_back" }, -- the previous workspace
        -- Sway's scratchpad: hide the focused window there; show a hidden one floating in the
        -- middle of the focused monitor, hide it again, or show the next one.
        { mods = { mod, "Shift" }, key = "minus", action = "move_to_scratchpad" },
        { mods = { mod }, key = "minus", action = "scratchpad_show" },
        { mods = { mod, "Shift" }, key = "r", action = "reload" },
        -- Screenshots run grim (and slurp to select a region).
        { mods = {}, key = "Print", action = "screenshot", mode = "region" },
        { mods = { "Shift" }, key = "Print", action = "screenshot", mode = "output" },
        { mods = { mod }, key = "Print", action = "screenshot", mode = "window" },
    },
}
