// SPDX-License-Identifier: GPL-3.0-or-later
#include "shaode/config.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rejects(const std::string &source) {
    try {
        (void)shaode::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "example config path required");
        auto config = shaode::load_config(argv[1]);
        require(config.bindings.size() == 40, "example shortcuts missing");
        require(config.binding(SH_ALT, XKB_KEY_Tab)->action == SH_SWITCHER_NEXT &&
                    config.binding(SH_ALT | SH_SHIFT, XKB_KEY_Tab)->action == SH_SWITCHER_PREV,
                "example window switcher bindings missing");
        require(config.shell.enabled && config.shell.panel_height == 52 &&
                    config.shell.launchers.empty(),
                "example shell settings missing");
        auto pinnedCommand = shaode::parse_config(
            "return {shell={launchers={{name='Home',icon='user-home',command={'xdg-open','.'}}}}}");
        require(pinnedCommand.shell.launchers.size() == 1 &&
                    pinnedCommand.shell.launchers.front().command == shaode::Command{"xdg-open", "."},
                "pinned command arguments changed");
        auto *spawn = config.binding(SH_LOGO, XKB_KEY_q);
        require(spawn && spawn->command == shaode::Command{"kitty"}, "spawn argv mismatch");
        require(config.binding(SH_LOGO | SH_SHIFT | 2, XKB_KEY_R)->action == SH_RELOAD,
                "shifted shortcut or CapsLock normalization failed");
        require(!config.binding(SH_LOGO | SH_CTRL, XKB_KEY_q), "extra modifiers matched");
        auto *fullscreen = config.binding(SH_LOGO, XKB_KEY_f);
        require(fullscreen && fullscreen->action == SH_FULLSCREEN, "fullscreen binding missing");
        auto *move = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_3);
        require(move && move->action == SH_MOVE_TO_WORKSPACE && move->workspace == 3,
                "move-to-workspace binding missing");
        require(config.settings.workspaces == 4, "example workspace count changed");
        require(!config.settings.tiling, "example starts tiled");
        auto *toggle = config.binding(SH_LOGO, XKB_KEY_s);
        require(toggle && toggle->action == SH_TOGGLE_TILING, "tiling toggle binding missing");
        require(shaode::parse_config("return {layout={tiling=true}}").settings.tiling,
                "layout.tiling not parsed");
        auto *focus = config.binding(SH_LOGO, XKB_KEY_Left);
        require(focus && focus->action == SH_FOCUS_LEFT, "directional focus binding missing");
        auto *move_window = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_Right);
        require(move_window && move_window->action == SH_MOVE_RIGHT, "move-window binding missing");
        auto *resize = config.binding(SH_LOGO | SH_CTRL | SH_SHIFT, XKB_KEY_Right);
        require(resize && resize->action == SH_RESIZE_RIGHT &&
                    resize->amount == shaode::default_resize_amount,
                "resize binding missing");
        require(config.settings.keyboard_resize, "keyboard resizing off by default");
        auto resizing = shaode::parse_config(
            "return {features={keyboard_resize=false},"
            "bindings={{mods={'Alt'},key='l',action='resize_left',amount=15}}}");
        require(!resizing.settings.keyboard_resize && resizing.bindings[0].amount == 15,
                "features or resize amount not parsed");
        rejects("return {features={keyboard_resize='no'}}");
        rejects("return {features={no_such_feature=true}}");
        rejects("return {features={true}}");
        rejects("return {features=true}");
        rejects("return {bindings={{key='l',action='close',amount=10}}}");
        rejects("return {bindings={{key='l',action='resize_up',amount=0}}}");
        rejects("return {bindings={{key='l',action='resize_up',amount=1.5}}}");
        auto *launcher = config.binding(SH_LOGO, XKB_KEY_r);
        require(launcher && launcher->action == SH_LAUNCHER, "launcher binding missing");
        require(shaode::parse_action("toggle_floating") == SH_TOGGLE_FLOATING,
                "toggle_floating action missing");
        rejects("return {layout={tiling='yes'}}");
        auto *back = config.binding(SH_LOGO, XKB_KEY_Tab);
        require(back && back->action == SH_WORKSPACE_BACK && back->workspace == 0,
                "workspace_back binding missing");
        require(!config.settings.workspace_back_and_forth,
                "example turns on workspace back-and-forth");
        require(!shaode::parse_config("return {}").settings.workspace_back_and_forth &&
                    !shaode::parse_config("return {features={}}").settings.workspace_back_and_forth,
                "workspace back-and-forth is on by default");
        require(shaode::parse_config("return {features={workspace_back_and_forth=true}}")
                    .settings.workspace_back_and_forth,
                "features.workspace_back_and_forth not parsed");
        rejects("return {features={workspace_back_and_forth='yes'}}");
        require(config.settings.sticky && shaode::parse_config("return {}").settings.sticky &&
                    shaode::parse_config("return {features={}}").settings.sticky,
                "sticky windows are off by default");
        require(!shaode::parse_config("return {features={sticky=false}}").settings.sticky,
                "features.sticky not parsed");
        rejects("return {features={sticky='no'}}");
        auto *sticky = config.binding(SH_LOGO, XKB_KEY_p);
        require(sticky && sticky->action == SH_TOGGLE_STICKY, "sticky binding missing");
        rejects("return {features={bogus=true}}");
        rejects("return {features={true}}");
        rejects("return {features=true}");
        rejects("return {bindings={{mods={'Super'},key='Tab',action='workspace_back',workspace=2}}}");
        auto *region = config.binding(0, XKB_KEY_Print);
        auto *whole = config.binding(SH_SHIFT, XKB_KEY_Print);
        auto *window = config.binding(SH_LOGO, XKB_KEY_Print);
        require(region && region->action == SH_SCREENSHOT &&
                    region->screenshot == SH_SCREENSHOT_REGION && whole &&
                    whole->screenshot == SH_SCREENSHOT_OUTPUT && window &&
                    window->screenshot == SH_SCREENSHOT_WINDOW,
                "screenshot bindings missing");
        require(config.screenshots.directory.empty() && config.screenshots.clipboard &&
                    config.screenshots.notify,
                "example screenshot settings changed");
        auto shots = shaode::parse_config(
            "return {screenshots={directory='~/Shots',clipboard=false,notify=false},"
            "bindings={{mods={'Alt'},key='Print',action='screenshot'}}}");
        require(shots.screenshots.directory == "~/Shots" && !shots.screenshots.clipboard &&
                    !shots.screenshots.notify &&
                    shots.bindings[0].screenshot == SH_SCREENSHOT_REGION,
                "screenshot settings not parsed");
        require(shaode::parse_screenshot_mode("window") == SH_SCREENSHOT_WINDOW,
                "screenshot mode names differ");
        require(shaode::parse_config("return {screenshots={directory='/tmp/x'}}")
                        .screenshots.directory == "/tmp/x",
                "absolute screenshot directory not parsed");
        rejects("return {screenshots={directory='Shots'}}");
        require(config.window_buttons == "appmenu:minimize,maximize,close",
                "default window buttons changed");
        require(shaode::parse_config("return {windows={buttons='close'}}").window_buttons ==
                        "close" &&
                    shaode::parse_config("return {windows={buttons=''}}").window_buttons.empty(),
                "windows.buttons not parsed");
        rejects("return {windows={buttons=\"close'\"}}");
        rejects("return {screenshots={directory=1}}");
        rejects("return {screenshots={clipboard='yes'}}");
        rejects("return {screenshots={format='jpeg'}}");
        rejects("return {bindings={{mods={},key='Print',action='screenshot',mode='screen'}}}");
        rejects("return {bindings={{mods={},key='Print',action='close',mode='window'}}}");
        auto outputs =
            shaode::parse_config("return {outputs={order={'HDMI-A-1','DP-3'},primary='DP-3'}}");
        require(outputs.settings.output_count == 2 &&
                    std::string(outputs.settings.output_order[1]) == "DP-3" &&
                    std::string(outputs.settings.primary_output) == "DP-3",
                "output order or primary not parsed");
        rejects("return {outputs={order={'DP-1','DP-1'}}}");
        rejects("return {outputs={order={''}}}");
        rejects("return {outputs={order={'1','2','3','4','5','6','7','8','9'}}}");
        rejects("return {outputs={primary=1}}");
        rejects("return {outputs={position={}}}");
        auto monitors = shaode::parse_config(
            "return {outputs={monitors={['DP-3']={mode='2560x1440@143.98',scale=1.25,"
            "position={x=-2048,y=0},transform=1},['HDMI-A-1']={enabled=false}}}}");
        require(monitors.settings.monitor_count == 2, "monitors not parsed");
        for (int i = 0; i < 2; ++i) {
            const auto &m = monitors.settings.monitors[i];
            if (std::string(m.name) == "DP-3")
                require(m.enabled && m.width == 2560 && m.height == 1440 && m.refresh == 143980 &&
                            m.scale == 1.25F && m.positioned && m.x == -2048 && m.y == 0 &&
                            m.transform == 1,
                        "monitor settings mismatch");
            else
                require(std::string(m.name) == "HDMI-A-1" && !m.enabled && m.width == 0 &&
                            !m.positioned,
                        "disabled monitor mismatch");
        }
        auto plain = shaode::parse_config("return {outputs={monitors={X={mode='800x600'}}}}");
        require(plain.settings.monitors[0].refresh == 0 && plain.settings.monitors[0].enabled,
                "mode without refresh mismatch");
        rejects("return {outputs={monitors={'DP-1'}}}");
        rejects("return {outputs={monitors={['']={}}}}");
        rejects("return {outputs={monitors={X={mode='2560x1440@'}}}}");
        rejects("return {outputs={monitors={X={mode='2560x1440x'}}}}");
        rejects("return {outputs={monitors={X={mode='0x1440'}}}}");
        rejects("return {outputs={monitors={X={scale=0}}}}");
        rejects("return {outputs={monitors={X={transform=8}}}}");
        rejects("return {outputs={monitors={X={position={x=1}}}}}");
        rejects("return {outputs={monitors={X={position={x=1,y=2,z=3}}}}}");
        rejects("return {outputs={monitors={X={refresh=60}}}}");
        auto described = shaode::parse_config(
            "return {outputs={monitors={['desc:ASUSTek COMPUTER INC VG27AQ3A']={vrr=true}}}}");
        require(std::string(described.settings.monitors[0].name) ==
                        "desc:ASUSTek COMPUTER INC VG27AQ3A" &&
                    described.settings.monitors[0].vrr,
                "description key or vrr not parsed");
        rejects("return {outputs={monitors={X={vrr='on'}}}}");
        auto tiled =
            shaode::parse_config("return {layout={tiling=true},"
                                 "outputs={monitors={A={tiling=false},B={tiling=true},C={}}}}");
        require(tiled.settings.tiling && tiled.settings.monitor_count == 3,
                "per-monitor tiling not parsed");
        for (int i = 0; i < 3; ++i) {
            const auto &m = tiled.settings.monitors[i];
            int expected = std::string(m.name) == "A" ? 0 : std::string(m.name) == "B" ? 1 : -1;
            require(m.tiling == expected, "per-monitor tiling mixed up");
        }
        require(plain.settings.monitors[0].tiling == -1, "monitor tiling should follow layout");
        rejects("return {outputs={monitors={X={tiling='yes'}}}}");
        require(shaode::parse_action("workspace_next") == SH_WORKSPACE_NEXT,
                "control action names differ from Lua");
        rejects("return {bindings={{mods={'Alt'},key='1',action='workspace'}}}");
        rejects("return {bindings={{mods={'Alt'},key='1',action='workspace',workspace=5}}}");
        rejects("return {layout={workspaces=2},bindings={{mods={'Alt'},key='1',"
                "action='move_to_workspace',workspace=3}}}");
        rejects("return {bindings={{mods={'Alt'},key='1',action='close',workspace=1}}}");
        rejects("return {layout={workspaces=0}}");
        rejects("return {layout={workspaces=11}}");
        auto computed = shaode::parse_config("local gap = 3; return {layout={gap=gap*2}}");
        require(computed.settings.gap_inner == 6 && computed.settings.gap_outer == 6,
                "Lua evaluation failed");
        rejects("return {layout={gap=-1}}");
        auto gaps = shaode::parse_config("return {layout={gap=4,gap_outer=10}}");
        require(gaps.settings.gap_inner == 4 && gaps.settings.gap_outer == 10,
                "gap_inner/gap_outer not parsed");
        rejects("return {layout={gap_inner=101}}");
        auto windows = shaode::parse_config(
            "return {windows={border_width=2,border_color='#ff000080',"
            "border_inactive_color='#00ff00',opacity=0.95,inactive_opacity=0.8,"
            "rules={{app_id='^firefox$',opacity=0.9},{app_id='code',opacity=0.7,"
            "inactive_opacity=0.6}}}}");
        const auto &ws = windows.settings;
        require(ws.border_width == 2 && ws.border_active[0] > 0.50F &&
                    ws.border_active[0] < 0.51F && ws.border_active[3] > 0.50F &&
                    ws.border_active[3] < 0.51F && ws.border_inactive[1] == 1.0F &&
                    ws.border_inactive[3] == 1.0F,
                "border settings not parsed or not premultiplied");
        require(windows.window_opacity("firefox", true) == 0.9F &&
                    windows.window_opacity("firefox", false) == 0.9F &&
                    windows.window_opacity("code-oss", false) == 0.6F &&
                    windows.window_opacity("firefox-esr", true) == 0.95F &&
                    windows.window_opacity("", false) == 0.8F,
                "window opacity rules mismatch");
        auto opaque = shaode::parse_config("return {}");
        require(opaque.window_opacity("x", false) == 1 && opaque.settings.border_width == 0,
                "window defaults changed");
        rejects("return {windows={border_width=21}}");
        require(opaque.settings.scratchpad && config.settings.scratchpad,
                "the scratchpad should default to on");
        require(!shaode::parse_config("return {features={scratchpad=false}}").settings.scratchpad,
                "features.scratchpad not parsed");
        rejects("return {features={scratchpad=1}}");
        rejects("return {features={no_such_feature=true}}");
        rejects("return {features=true}");
        auto *hide = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_minus);
        require(hide && hide->action == SH_MOVE_TO_SCRATCHPAD, "scratchpad binding missing");
        auto *show = config.binding(SH_LOGO, XKB_KEY_minus);
        require(show && show->action == SH_SCRATCHPAD_SHOW, "scratchpad_show binding missing");
        auto input = shaode::parse_config(
            "return {mouse={speed=-0.5,acceleration='flat',natural_scroll=false,"
            "focus_follows=false},"
            "touchpad={natural_scroll=true,tap_to_click=true,disable_while_typing=false}}");
        const auto &is = input.settings;
        require(is.pointer_speed_set && is.pointer_speed == -0.5 && is.pointer_accel == 0 &&
                    is.mouse_natural_scroll == 0 && is.touchpad_natural_scroll == 1 &&
                    is.touchpad_tap == 1 && is.touchpad_dwt == 0 && is.mouse_modifier == SH_ALT &&
                    !is.focus_follows_mouse,
                "pointer settings not parsed");
        const auto &defaults = opaque.settings;
        require(!defaults.pointer_speed_set && defaults.pointer_accel == -1 &&
                    defaults.mouse_natural_scroll == -1 && defaults.touchpad_tap == -1 &&
                    defaults.focus_follows_mouse,
                "pointer defaults must leave devices alone");
        require(defaults.animations && defaults.animation_duration == 120,
                "animations should be on for 120 ms by default");
        require(config.settings.animations, "example turns animations off");
        auto still = shaode::parse_config("return {animations={enabled=false,duration=200}}");
        require(!still.settings.animations && still.settings.animation_duration == 200,
                "animations not parsed");
        require(shaode::parse_config("return {animations={duration=80}}").settings.animations,
                "a duration alone should keep animations on");
        rejects("return {animations={enabled='no'}}");
        rejects("return {animations={duration=0}}");
        rejects("return {animations={duration=1.5}}");
        rejects("return {animations={duration=5000}}");
        rejects("return {animations={curve='linear'}}");
        rejects("return {animations=true}");
        rejects("return {mouse={speed=2}}");
        rejects("return {mouse={acceleration='fast'}}");
        rejects("return {touchpad={tap_to_click=1}}");
        rejects("return {touchpad={scroll_factor=2}}");
        rejects("return {windows={border_color='red'}}");
        rejects("return {windows={opacity=0}}");
        rejects("return {windows={opacity=1.5}}");
        rejects("return {windows={rules={{app_id='('}}}}");
        rejects("return {windows={rules={{opacity=0.5}}}}");
        rejects("return {windows={rules={{app_id='x',class='y'}}}}");

        // Rule actions: every matching rule applies in order, later ones winning.
        auto ruled = shaode::parse_config(
            "return {layout={workspaces=5},windows={opacity=0.95,rules={"
            "{app_id='^pavucontrol$',floating=true,size={640,480},position='center'},"
            "{app_id='pavu',title='Volume',focus=false,position={x=10,y=20}},"
            "{title='^Mail',workspace=2,output='desc:Dell U2720Q',floating=false},"
            "{app_id='^mpv$',fullscreen=true,maximize=true,opacity=0.5},"
            "{app_id='^mpv$',output='DP-1',size={width=100,height=200},fullscreen=false},"
            "{app_id='^mpv$',sticky=true}}}}");
        require(ruled.settings.window_rules, "window rules should be on by default");
        auto pavu = ruled.window_actions("pavucontrol", "Volume Control");
        require(pavu.floating == true && pavu.size == std::pair{640, 480} &&
                    pavu.position == shaode::WindowActions::Position::At && pavu.x == 10 &&
                    pavu.y == 20 && pavu.focus == false && !pavu.workspace && !pavu.output,
                "window rule actions did not merge in order");
        auto pavu_c = ruled.window_actions("pavucontrol", "Other").to_c();
        require(pavu_c.floating == 1 && pavu_c.width == 640 && pavu_c.height == 480 &&
                    pavu_c.position == SH_RULE_POSITION_CENTER && !pavu_c.no_focus &&
                    pavu_c.workspace == 0 && !pavu_c.output[0] && !pavu_c.fullscreen,
                "title regex matched the wrong window or C rule mismatch");
        auto mail = ruled.window_actions("thunderbird", "Mail - Inbox").to_c();
        require(mail.floating == 0 && mail.workspace == 2 &&
                    std::string(mail.output) == "desc:Dell U2720Q" &&
                    mail.position == SH_RULE_POSITION_UNSET,
                "title-only rule mismatch");
        auto mpv = ruled.window_actions("mpv", "").to_c();
        require(!mpv.fullscreen && mpv.maximize && std::string(mpv.output) == "DP-1" &&
                    mpv.width == 100 && mpv.height == 200 && mpv.floating == -1 && mpv.sticky,
                "later rule did not override an earlier one");
        require(ruled.window_actions("kitty", "My Mail").empty(), "a rule matched the wrong window");
        // Opacity: the first rule that sets it wins; action-only rules leave it alone.
        require(ruled.window_opacity("pavucontrol", "Volume", true) == 0.95F &&
                    ruled.window_opacity("mpv", "", true) == 0.5F,
                "action-only rules changed opacity");
        auto titled = shaode::parse_config(
            "return {windows={rules={{title='^Picture',opacity=0.8},{app_id='x'}}}}");
        require(titled.window_opacity("firefox", "Picture-in-Picture", true) == 0.8F &&
                    titled.window_opacity("firefox", "Other", true) == 1 &&
                    titled.window_opacity("x", "", false) == 1,
                "opacity rules by title mismatch");
        // features.window_rules = false drops the actions but keeps opacity rules.
        auto off = shaode::parse_config(
            "return {features={window_rules=false},windows={rules={"
            "{app_id='^mpv$',floating=true,opacity=0.5}}}}");
        require(!off.settings.window_rules && off.window_actions("mpv", "").empty() &&
                    off.window_opacity("mpv", "", true) == 0.5F,
                "features.window_rules = false not honoured");
        rejects("return {features={window_rules='no'}}");
        rejects("return {features={bogus=true}}");
        rejects("return {features={true}}");
        rejects("return {features=false}");
        rejects("return {windows={rules={{floating=true}}}}");
        rejects("return {windows={rules={{title='('}}}}");
        rejects("return {windows={rules={{app_id='x',floating='yes'}}}}");
        rejects("return {windows={rules={{app_id='x',focus=0}}}}");
        rejects("return {windows={rules={{app_id='x',sticky='yes'}}}}");
        rejects("return {windows={rules={{app_id='x',fullscreen='true'}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=5}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=0}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=1.5}}}}");
        rejects("return {windows={rules={{app_id='x',output=''}}}}");
        rejects("return {windows={rules={{app_id='x',output='desc:'}}}}");
        rejects("return {windows={rules={{app_id='x',output=3}}}}");
        rejects("return {windows={rules={{app_id='x',size={640}}}}}");
        rejects("return {windows={rules={{app_id='x',size={640,480,1}}}}}");
        rejects("return {windows={rules={{app_id='x',size={0,480}}}}}");
        rejects("return {windows={rules={{app_id='x',size={w=640,h=480}}}}}");
        rejects("return {windows={rules={{app_id='x',size='640x480'}}}}");
        rejects("return {windows={rules={{app_id='x',position='middle'}}}}");
        rejects("return {windows={rules={{app_id='x',position={x=1}}}}}");
        rejects("return {windows={rules={{app_id='x',position={1.5,2}}}}}");
        rejects("return {windows={rules={{app_id='x',position=true}}}}");
        rejects("return {keyboard={repeat_rate='25'}}");
        rejects("return {appearance={background='#oops00'}}");
        rejects("return {layuot={gap=2}}");
        rejects("return {version=2}");
        rejects("return {shell={enabled='yes'}}");
        rejects("return {shell={panel_height=0}}");
        auto bar = shaode::parse_config(
            "return {shell={panel_position='top',panel_margin={top=6,left=10,right=10},"
            "panel_radius=12,font='JetBrainsMono Nerd Font',font_size=13,icons_only=false,"
            "group_windows=false,panel_color='#151e2ccc'}}");
        require(bar.shell.panel_top && bar.shell.panel_margin[0] == 6 &&
                    bar.shell.panel_margin[1] == 10 && bar.shell.panel_margin[2] == 0 &&
                    bar.shell.panel_margin[3] == 10 && bar.shell.panel_radius == 12 &&
                    bar.shell.font == "JetBrainsMono Nerd Font" && bar.shell.font_size == 13 &&
                    bar.shell.panel_color == "#151e2ccc" && !bar.shell.icons_only &&
                    !bar.shell.group_windows,
                "bar settings not parsed");
        auto even = shaode::parse_config("return {shell={panel_margin=8}}");
        require(even.shell.panel_margin[0] == 8 && even.shell.panel_margin[3] == 8 &&
                    !even.shell.panel_top && even.shell.icons_only && even.shell.group_windows,
                "single panel margin not parsed");
        rejects("return {shell={panel_position='left'}}");
        rejects("return {shell={panel_margin=-1}}");
        rejects("return {shell={panel_margin={middle=1}}}");
        rejects("return {shell={panel_radius=51}}");
        rejects("return {shell={font_size=2}}");
        rejects("return {shell={icons_only='yes'}}");
        rejects("return {shell={group_windows=1}}");
        rejects("return {shell={accent='#12345'}}");
        rejects("return {appearance={background='#11223344'}}");
        rejects("return {shell={accent='red'}}");
        rejects("return {shell={launchers={{name='',command={'kitty'}}}}}");
        rejects("return {shell={launchers={{name='Terminal',command='kitty'}}}}");
        rejects("return true");
        rejects("return {startup={{}}}");
        rejects("return {startup={{'kitty', [3]='bad'}}}");
        rejects("return {startup={{'kitty\\0bad'}}}");
        rejects("return {bindings={{mods={'Hyper'}, key='a', action='quit'}}}");
        rejects("return {bindings={{mods={}, key='NotAKey', action='quit'}}}");
        rejects("return {bindings={{mods={}, key='a', action='unknown'}}}");
        rejects("local b={mods={'Alt'},key='a',action='quit'}; return {bindings={b,b}}");
        rejects("while true do end");
        rejects("os.execute('false')");
        // A configuration extending the defaults holds only its changes.
        setenv("SHAODE_DEFAULT_CONFIG", argv[1], 1);
        auto bare = shaode::parse_config("return {extends='default'}");
        require(bare.bindings.size() == 40 && bare.shell.launchers.empty() &&
                    bare.settings.workspaces == 4,
                "extends did not supply the defaults");
        auto layered = shaode::parse_config(
            "return {extends='default', layout={gap=3}, bindings={"
            "{mods={'Super'}, key='v', action='none'},"
            "{mods={'Super'}, key='q', action='spawn', command={'foot'}},"
            "{mods={'Super'}, key='e', action='spawn', command={'dolphin'}}}}");
        require(layered.settings.gap_inner == 3 && layered.settings.workspaces == 4,
                "extending configuration settings not layered over the defaults");
        require(layered.bindings.size() == 40 && !layered.binding(SH_LOGO, XKB_KEY_v),
                "action none did not remove a default binding");
        require(layered.binding(SH_LOGO, XKB_KEY_q)->command == shaode::Command{"foot"} &&
                    layered.binding(SH_LOGO, XKB_KEY_e)->command == shaode::Command{"dolphin"},
                "own bindings did not take their keys from the defaults");
        require(layered.binding(SH_LOGO, XKB_KEY_s)->action == SH_TOGGLE_TILING,
                "untouched default binding missing");
        require(shaode::parse_config("return {bindings={{mods={}, key='a', action='none'}}}")
                    .bindings.empty(),
                "action none left a binding");
        require(!bare.settings.workspace_back_and_forth &&
                    shaode::parse_config(
                        "return {extends='default', features={workspace_back_and_forth=true}}")
                        .settings.workspace_back_and_forth,
                "features not layered over the defaults");
        rejects("return {extends='default', features={bogus=true}}");
        rejects("return {extends='other'}");
        rejects("local b={mods={'Alt'},key='a',action='quit'}; "
                "return {extends='default', bindings={b,b}}");
        auto buttons = shaode::parse_config(
            "return {bindings={"
            "{button='side', app_id='^kitty$', desktop=true, action='close'},"
            "{button='extra', app_id='^kitty$', action='spawn', command={'kitty'}},"
            "{button='extra', desktop=true, action='spawn', command={'foot'}},"
            "{button='middle', app_id='^firefox$', action='none'},"
            "{button='middle', action='fullscreen'},"
            "{mods={'Super'}, button='left', action='quit'}}}");
        require(buttons.bindings.size() == 6, "button bindings missing");
        require(buttons.button_binding(0, 0x113, SH_POINTER_WINDOW, "kitty")->action == SH_CLOSE &&
                    buttons.button_binding(0, 0x113, SH_POINTER_DESKTOP, "")->action == SH_CLOSE,
                "side button binding did not match a terminal or the desktop");
        require(!buttons.button_binding(0, 0x113, SH_POINTER_WINDOW, "firefox") &&
                    !buttons.button_binding(0, 0x113, SH_POINTER_OTHER, "") &&
                    !buttons.button_binding(SH_LOGO, 0x113, SH_POINTER_WINDOW, "kitty"),
                "side button binding took another window's, a panel's, or a modified click");
        require(buttons.button_binding(0, 0x114, SH_POINTER_WINDOW, "kitty")->command ==
                        shaode::Command{"kitty"} &&
                    buttons.button_binding(0, 0x114, SH_POINTER_DESKTOP, "")->command ==
                        shaode::Command{"foot"},
                "bindings sharing a button did not match in order");
        require(!buttons.button_binding(0, 0x112, SH_POINTER_WINDOW, "firefox") &&
                    buttons.button_binding(0, 0x112, SH_POINTER_OTHER, "")->action ==
                        SH_FULLSCREEN,
                "button action none or an unconditional button binding misbehaved");
        require(buttons.button_binding(SH_LOGO, 0x110, SH_POINTER_WINDOW, "x")->action == SH_QUIT &&
                    !buttons.binding(0, 0),
                "modified button binding missing, or a button matched as a key");
        rejects("return {bindings={{button='wheel', action='close'}}}");
        rejects("return {bindings={{button='side', key='a', action='close'}}}");
        rejects("return {bindings={{key='a', app_id='x', action='close'}}}");
        rejects("return {bindings={{key='a', desktop=true, action='close'}}}");
        rejects("return {bindings={{button='side', app_id='(', action='close'}}}");
        // Failed reload leaves the previously active value intact.
        try {
            config = shaode::parse_config("return {layout={gap=999}}");
        } catch (const std::exception &) {
        }
        require(config.settings.gap_inner == 8 && config.bindings.size() == 40,
                "failed reload changed active configuration");
        std::cout << "Configuration validation, bindings, and transactional loading passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
