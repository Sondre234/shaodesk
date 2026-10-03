// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shaode/backend.h"
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace shaode {
using Command = std::vector<std::string>;
// Pixels a resize_* action moves an edge by, unless its binding or request gives `amount`.
constexpr int default_resize_amount = 40;
constexpr int max_resize_amount = 4000;

struct Binding {
    uint32_t modifiers;
    uint32_t keysym; // 0 for a mouse button binding
    uint32_t button = 0; // a Linux BTN_* code; 0 for a key binding
    // A button binding acts only over a window whose app ID matches `app_id` (an ECMAScript
    // regex, searched) or, with `desktop`, over the bare desktop; with neither, anywhere.
    // Elsewhere the click reaches the application under the pointer.
    std::string app_id;
    std::optional<std::regex> pattern;
    bool desktop = false;
    sh_action action;
    Command command;
    int workspace = 0; // for workspace and move_to_workspace, from 1
    sh_screenshot_mode screenshot = SH_SCREENSHOT_REGION; // for screenshot
    int amount = default_resize_amount; // for resize_*, in pixels
};

struct Launcher {
    std::string name;
    std::string icon;
    Command command;
};

// What rules do to a window as it opens; unset fields leave it to the defaults.
struct WindowActions {
    enum class Position { Unset, Center, At };
    std::optional<bool> floating, fullscreen, maximize, focus, sticky;
    std::optional<int> workspace;      // from 1
    std::optional<std::string> output; // connector, or "desc:" and the start of its description
    std::optional<std::pair<int, int>> size;
    Position position = Position::Unset;
    int x = 0, y = 0; // with Position::At, from the top-left of the output's usable area
    bool empty() const;
    // Fields `other` sets replace these.
    void merge(const WindowActions &other);
    sh_window_rule to_c() const;
};

// Windows whose app ID and title match `app_id` and `title` (ECMAScript regexes, searched;
// an absent one matches anything) get these opacities and actions.
struct WindowRule {
    std::string app_id, title;
    std::optional<std::regex> pattern, title_pattern;
    bool sets_opacity = true; // takes part in window_opacity
    float opacity = 1, inactive_opacity = 1;
    WindowActions actions;
    bool matches(const std::string &app_id, const std::string &title) const;
};

struct ShellConfig {
    bool enabled = true;
    int panel_height = 52;
    bool panel_top = false;             // panel_position = "top"
    int panel_margin[4] = {0, 0, 0, 0}; // top, right, bottom, left: a floating bar
    int panel_radius = 0;
    std::string font;   // family; empty: the Qt default
    int font_size = 12; // taskbar text, in pixels
    bool icons_only = true; // taskbar buttons show the window's icon, its title as a tooltip
    bool group_windows = true; // one taskbar button per application, its windows listed on hover
    std::string accent = "#7da8ff";
    std::string panel_color = "#151e2c";
    std::string text_color = "#edf2fa";
    std::string wallpaper;
    std::vector<Launcher> launchers;
};

struct ScreenshotConfig {
    std::string directory; // absolute or "~/..."; empty: $XDG_PICTURES_DIR/Screenshots
    bool clipboard = true; // also copy the image with wl-copy
    bool notify = true;    // announce the file with notify-send, when it is installed
};

struct Config {
    sh_settings settings{.background = {25 / 255.0F, 33 / 255.0F, 46 / 255.0F, 1.0F},
                         .mouse_modifier = SH_ALT,
                         .repeat_rate = 25,
                         .repeat_delay = 600,
                         .gap_inner = 8,
                         .gap_outer = 8,
                         .keyboard_layout = "us",
                         .keyboard_options = "",
                         .xwayland = true,
                         .tiling = false,
                         .workspaces = 4,
                         .output_order = {},
                         .output_count = 0,
                         .primary_output = "",
                         .monitors = {},
                         .monitor_count = 0,
                         .border_width = 0,
                         .border_active = {0.49F, 0.66F, 1.0F, 1.0F},
                         .border_inactive = {0.25F, 0.29F, 0.36F, 1.0F},
                         .pointer_speed = 0,
                         .pointer_speed_set = false,
                         .pointer_accel = -1,
                         .mouse_natural_scroll = -1,
                         .touchpad_natural_scroll = -1,
                         .touchpad_tap = -1,
                         .touchpad_dwt = -1,
                         .focus_follows_mouse = true,
                         .animations = true,
                         .animation_duration = 120,
                         .workspace_back_and_forth = false,
                         .scratchpad = true,
                         .sticky = true,
                         .keyboard_resize = true,
                         .window_rules = true};
    std::vector<Binding> bindings;
    std::vector<Command> startup;
    ShellConfig shell;
    ScreenshotConfig screenshots;
    float opacity = 1, inactive_opacity = 1;
    std::vector<WindowRule> window_rules;
    // GTK's button layout for client-decorated windows (Firefox's tab strip); empty: GTK's own.
    std::string window_buttons = "appmenu:minimize,maximize,close";

    const Binding *binding(uint32_t modifiers, uint32_t keysym) const;
    // The first button binding for what lies under the pointer, or nothing (the click belongs
    // to the application, as does one whose first match is action = "none").
    const Binding *button_binding(uint32_t modifiers, uint32_t button, sh_pointer_target target,
                                  const std::string &app_id) const;
    // The first matching rule that sets opacity decides; otherwise the windows.opacity defaults.
    float window_opacity(const std::string &app_id, const std::string &title, bool active) const;
    float window_opacity(const std::string &app_id, bool active) const {
        return window_opacity(app_id, "", active);
    }
    // The actions of every matching rule, later rules winning; none with
    // features.window_rules = false.
    WindowActions window_actions(const std::string &app_id, const std::string &title) const;
};

// Maps a Lua/control-socket action name; throws for unknown names.
sh_action parse_action(const std::string &name);
bool action_takes_workspace(sh_action action);
bool action_takes_amount(sh_action action); // resize_*: pixels, 1 to max_resize_amount
// "region", "output", or "window"; throws for other names.
sh_screenshot_mode parse_screenshot_mode(const std::string &name);

// Parse into a fresh value; callers replace the active configuration only on success.
// A configuration's `theme = "FILE"` (relative to `directory`) supplies every setting it omits.
// `extends = "default"` then supplies what both omit from the default configuration
// ($SHAODE_DEFAULT_CONFIG, else the installed one); its bindings yield to the configuration's own
// on the same keys, and a binding with action = "none" removes a default one.
Config load_config(const std::filesystem::path &path);
Config parse_config(const std::string &source, const std::string &name = "config",
                    const std::filesystem::path &directory = {});
// Settings, as "shell.accent", that the configuration at `path` sets itself although its theme
// file sets them too. Nothing when the configuration names no theme.
std::optional<std::vector<std::string>> shadowed_settings(const std::filesystem::path &path);
} // namespace shaode
