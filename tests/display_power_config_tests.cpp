// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of monitors turned off in the layout: the display actions.
#include "shaodesk/config.hpp"
#include <iostream>
#include <stdexcept>

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

int main() {
    try {
        auto bound = shaodesk::parse_config(
            "return {bindings={{mods={'Super'},key='F1',action='display_off'},"
            "{mods={'Super'},key='F2',action='display_on',output='HDMI-A-1'},"
            "{mods={'Super'},key='F3',action='display_toggle',output='desc:Dell U27'}}}");
        const auto &b = bound.bindings;
        require(b.size() == 3 && b[0].action == SH_DISPLAY_OFF && b[0].output.empty() &&
                    b[1].action == SH_DISPLAY_ON && b[1].output == "HDMI-A-1" &&
                    b[2].action == SH_DISPLAY_TOGGLE && b[2].output == "desc:Dell U27",
                "display bindings not parsed");
        require(shaodesk::action_takes_display(SH_DISPLAY_OFF) &&
                    !shaodesk::action_takes_display(SH_LOCK) &&
                    !shaodesk::action_takes_output(SH_DISPLAY_TOGGLE),
                "display actions' targets");
        rejects("return {bindings={{key='l',action='display_off',output=''}}}");
        rejects("return {bindings={{key='l',action='display_off',output='desc:'}}}");
        rejects("return {bindings={{key='l',action='display_on',output=1}}}");
        rejects("return {bindings={{key='l',action='lock',output='HDMI-A-1'}}}");
        std::cout << "display power configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
