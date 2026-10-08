// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of what a monitor shows: mirroring another one.
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
    } catch (const std::exception &error) {
        std::cerr << "output_config: " << error.what() << '\n';
        return 1;
    }
    std::cout << "output configuration ok\n";
    return 0;
}
