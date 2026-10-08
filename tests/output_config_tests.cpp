// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of what a monitor shows: mirroring another one, and the display_mode action.
#include "shaodesk/config.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void rejects(const std::string &source) {
    try {
        (void)shaodesk::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
static const sh_monitor &monitor(const shaodesk::Config &config, const std::string &name) {
    for (int i = 0; i < config.settings.monitor_count; ++i)
        if (name == config.settings.monitors[i].name)
            return config.settings.monitors[i];
    throw std::runtime_error("no monitor " + name);
}

int main() {
    try {
        // A monitor mirrors another by connector or description; unset, it joins the layout.
        auto mirrored = shaodesk::parse_config(
            "return {outputs={monitors={['HDMI-A-1']={mirror='eDP-1',mode='1920x1080'},"
            "['DP-2']={mirror='desc:Dell U27'},['DP-3']={scale=1.5}}}}");
        require(std::string(monitor(mirrored, "HDMI-A-1").mirror) == "eDP-1" &&
                    monitor(mirrored, "HDMI-A-1").width == 1920,
                "mirror by connector not parsed");
        require(std::string(monitor(mirrored, "DP-2").mirror) == "desc:Dell U27",
                "mirror by description not parsed");
        require(!monitor(mirrored, "DP-3").mirror[0], "a monitor mirrors nothing unless told");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror=''}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror='desc:'}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror='HDMI-A-1'}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror=1}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror={'eDP-1'}}}}}");

        // display_mode takes one of Win+P's choices, or none for the popup.
        auto modes = shaodesk::parse_config(
            "return {bindings={{key='XF86Display',action='display_mode'},"
            "{mods={'Super'},key='F7',action='display_mode',mode='duplicate'},"
            "{switch='lid',state='open',action='display_mode',mode='extend'}}}");
        const auto &b = modes.bindings;
        require(b.size() == 3 && b[0].action == SH_DISPLAY_MODE &&
                    b[0].mode == SH_DISPLAY_MODE_STEP && b[1].mode == SH_DISPLAY_MODE_DUPLICATE &&
                    b[2].mode == SH_DISPLAY_MODE_EXTEND,
                "display_mode bindings not parsed");
        require(shaodesk::parse_display_mode("internal") == SH_DISPLAY_MODE_INTERNAL &&
                    shaodesk::parse_display_mode("external") == SH_DISPLAY_MODE_EXTERNAL,
                "display modes by name");
        require(shaodesk::parse_action("display_mode") == SH_DISPLAY_MODE, "display_mode action");
        rejects("return {bindings={{key='p',action='display_mode',mode='mirror'}}}");
        rejects("return {bindings={{key='p',action='display_mode',mode=2}}}");
        rejects("return {bindings={{key='p',action='display_mode',output='eDP-1'}}}");
    } catch (const std::exception &error) {
        std::cerr << "output_config: " << error.what() << '\n';
        return 1;
    }
    std::cout << "output configuration ok\n";
    return 0;
}
