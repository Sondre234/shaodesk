// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shaodesk/backend.h"
#include <array>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace shaodesk {
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
    std::string output; // for move_workspace_to_output and swap_workspaces: the target
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

// Which widgets the panel shows; each is switched off with `shell.widgets = { name = false }`.
struct ShellWidgets {
    bool workspaces = true; // this monitor's workspace numbers
    bool battery = true;    // charge and state, only where a battery exists
    bool network = true;    // connection state, only where a network interface exists
    bool volume = true;     // the default output's volume, only with a sound server
    bool clock = true;
    bool calendar = true; // clicking the clock opens a month calendar
    bool tiling = true;   // the tiling on/off button
    bool profiles = true; // the appearance profile picker, only with two or more profiles
    bool wallpapers = true; // the wallpaper picker
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
    bool software_renderer = true; // renderer = "software": Qt Quick without the GPU, faster to start
    bool group_windows = true; // one taskbar button per application, its windows listed on hover
    std::string accent = "#7da8ff";
    std::string panel_color = "#151e2c";
    std::string text_color = "#edf2fa";
    std::string wallpaper;
    std::string wallpapers; // the picker's folder: absolute or "~/..."; empty: $XDG_PICTURES_DIR/wallpapers
    ShellWidgets widgets;
    std::vector<Launcher> launchers;
};

struct ScreenshotConfig {
    std::string directory; // absolute or "~/..."; empty: $XDG_PICTURES_DIR/Screenshots
    bool clipboard = true; // also copy the image with wl-copy
    bool notify = true;    // announce the file with notify-send, when it is installed
};

// Lua `power`.
struct PowerConfig {
    // The screen locker the `lock` action starts; empty for none.
    Command lock_command{"swaylock", "-f"};
};

// Where the notification cards and the on-screen display sit on the focused monitor.
enum class Corner { TopRight, TopLeft, BottomRight, BottomLeft };

struct NotificationsConfig {
    bool enabled = true;     // serve org.freedesktop.Notifications and show the cards
    Corner position = Corner::TopRight;
    int timeout = 6000;      // milliseconds before a card without its own timeout goes; 0: never
    int max_visible = 4;     // cards shown at once; the rest wait in a queue
    bool dnd = false;        // start in do-not-disturb: no cards, everything goes to the history
    int width = 360;         // card width in pixels
    int history = 100;       // notifications kept in the history popover
};

struct OsdConfig {
    bool enabled = true;      // the on-screen display for volume, brightness and `shaodesk msg osd`
    bool top = false;         // position = "top": near the top edge instead of the bottom
    int timeout = 1500;       // milliseconds shown before it fades out
    bool volume = true;       // show it when the default output's volume or mute changes
    bool brightness = true;   // show it when a backlight's brightness changes
};

struct Config {
    sh_settings settings{.background = {25 / 255.0F, 33 / 255.0F, 46 / 255.0F, 1.0F},
                         .mouse_modifier = SH_ALT,
                         .repeat_rate = 25,
                         .repeat_delay = 600,
                         .gap_inner = 8,
                         .gap_outer = 8,
                         .keyboard_layout = "us",
                         .keyboard_variant = "",
                         .keyboard_model = "",
                         .keyboard_options = "",
                         .xwayland = true,
                         .tiling = false,
                         .tiling_per_workspace = false,
                         .tile_layout = 0,
                         .master_ratio = 0.55F,
                         .master_count = 1,
                         .scroll_follow = SH_SCROLL_FOLLOW_CENTER,
                         .scroll_width = 0.5F,
                         .scroll_step = 0.1F,
                         .scroll_presets = {1.0F / 3, 0.5F, 2.0F / 3, 1.0F},
                         .scroll_preset_count = 4,
                         .workspaces = 4,
                         .output_order = {},
                         .output_count = 0,
                         .primary_output = "",
                         .return_windows = true,
                         .monitors = {},
                         .monitor_count = 0,
                         .output_layouts = {},
                         .output_layout_count = 0,
                         .border_width = 0,
                         .corner_radius = 10,
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
                         .animation_speed = 1.0F,
                         .animation_late_ms = 80,
                         .animation_slide = 0.08F,
                         .animation_styles = {{120, {SH_CURVE_EASE_OUT, {}}},        // open
                                              {120, {SH_CURVE_EASE_OUT, {}}},        // close
                                              {120, {SH_CURVE_SPRING, {}}},          // move
                                              {120, {SH_CURVE_EASE_OUT_QUINT, {}}},  // workspace
                                              {120, {SH_CURVE_EASE_OUT, {}}},        // fullscreen
                                              {120, {SH_CURVE_EASE_OUT, {}}}},       // focus
                         .workspace_back_and_forth = false,
                         .scratchpad = true,
                         .sticky = true,
                         .keyboard_resize = true,
                         .window_rules = true,
                         .groups = true,
                         .group_join_new = true,
                         .overview = true,
                         .overview_gap = 24,
                         .overview_animation = true,
                         .overview_duration = 180,
                         .overview_strip = true,
                         .overview_hot_corner = 0,
                         .overview_dim = 0.86F,
                         .dim_inactive = 0,
                         .dim_duration = 180,
                         .activation = SH_ACTIVATION_URGENT,
                         .urgent_color = {1.0F, 0.62F, 0.39F, 1.0F},
                         .effects = {.peek_opacity = 0.12F,
                                     .peek_duration = 150,
                                     .night_light = false,
                                     .day_kelvin = 6500,
                                     .night_kelvin = 3400,
                                     .sunrise = 7 * 60,
                                     .sunset = 20 * 60,
                                     .latitude = 0,
                                     .longitude = 0,
                                     .located = false,
                                     .transition = 30,
                                     .corner_size = 2,
                                     .corner_delay = 150,
                                     .corner_mask = 0,
                                     .zoom_step = 1.25F,
                                     .zoom_max = 8,
                                     .zoom_duration = 150,
                                     .zoom_scroll_modifier = 0},
                         .swallow = false,
                         .swallow_terminals = {"kitty", "foot", "footclient", "Alacritty",
                                               "org.wezfurlong.wezterm", "com.mitchellh.ghostty",
                                               "xterm", "URxvt", "org.kde.konsole"},
                         .swallow_terminal_count = 9,
                         .swallow_exceptions = {},
                         .swallow_exception_count = 0,
                         .magnet = true,
                         .magnet_distance = 12,
                         .magnet_guides = true,
                         .magnet_bypass = SH_SHIFT,
                         .magnet_guide_color = {0.49F * 0.7F, 0.66F * 0.7F, 0.7F, 0.7F},
                         .placement = SH_PLACE_CASCADE,
                         .drag_strip = 6};
    // layout.workspace_names: the label of workspace N is names[N - 1]; "" or past the end: none.
    std::vector<std::string> workspace_names;
    // hot_corners: what each corner runs, as a control request; "" for nothing.
    std::array<std::string, 4> hot_corners;
    std::vector<Binding> bindings;
    std::vector<Command> startup;
    ShellConfig shell;
    NotificationsConfig notifications;
    OsdConfig osd;
    ScreenshotConfig screenshots;
    PowerConfig power;
    float opacity = 1, inactive_opacity = 1;
    std::vector<WindowRule> window_rules;
    // GTK's button layout for client-decorated windows (Firefox's tab strip); empty: GTK's own.
    std::string window_buttons = "appmenu:minimize,maximize,close";
    // profiles: their names, sorted, and the one in use ("" for none).
    std::vector<std::string> profiles;
    std::string profile;
    // auto_reload: reload when a Lua file beside the configuration is saved.
    bool auto_reload = true;

    // The workspace number, from 1, named `name` (or written as a number in range), else 0.
    int workspace_number(const std::string &name) const;
    // "N" or "N name": how a workspace is labelled.
    std::string workspace_label(int workspace) const;

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
// move_workspace_to_output and swap_workspaces: an output target, "left", "right", "next",
// "prev", a connector name, or "desc:..."
bool action_takes_output(sh_action action);
// Whether `target` reads as one of those (it says nothing of whether such an output exists).
bool valid_output_target(const std::string &target);
// "region", "output", or "window"; throws for other names.
sh_screenshot_mode parse_screenshot_mode(const std::string &name);

// Parse into a fresh value; callers replace the active configuration only on success.
// A configuration's `theme = "FILE"` (relative to `directory`) supplies every setting it omits.
// `extends = "default"` then supplies what both omit from the default configuration
// ($SHAODESK_DEFAULT_CONFIG, else the installed one); its bindings yield to the configuration's own
// on the same keys, and a binding with action = "none" removes a default one.
Config load_config(const std::filesystem::path &path);
// load_config, except that a configuration with an error gives the default one instead
// ($SHAODESK_DEFAULT_CONFIG, else the installed one, else the source tree's when shaodesk runs
// uninstalled, else the built-in values). `error` receives
// what was wrong, or "" when `path` loaded.
Config load_config_or_default(const std::filesystem::path &path, std::string &error);
// `profiles` lay settings over everything else: the one named `chosen` when there is such a
// profile, else the one `profile` names. load_config chooses the saved profile.
Config parse_config(const std::string &source, const std::string &name = "config",
                    const std::filesystem::path &directory = {}, const std::string &chosen = {});
// Where the profile picked from the shell or `shaodesk msg profile` is kept:
// $XDG_STATE_HOME/shaodesk/profile, else ~/.local/state/shaodesk/profile; empty without either.
std::filesystem::path profile_state_path();
// The saved profile name, or "" when none was saved.
std::string saved_profile();
// Saves `name` as the profile to use; throws when it cannot be written.
void save_profile(const std::string &name);
// Settings, as "shell.accent", that the configuration at `path` sets itself although its theme
// file sets them too. Nothing when the configuration names no theme.
std::optional<std::vector<std::string>> shadowed_settings(const std::filesystem::path &path);
} // namespace shaodesk
