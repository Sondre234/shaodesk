// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the power controls.
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
        auto defaults = shaodesk::parse_config("return {}");
        require(defaults.power.lock_command == shaodesk::Command{"swaylock", "-f"} &&
                    defaults.settings.lock_before_sleep,
                "power defaults");
        auto custom = shaodesk::parse_config(
            "return {power={lock_command={'gtklock','--daemonize'},lock_before_sleep=false}}");
        require(custom.power.lock_command == shaodesk::Command{"gtklock", "--daemonize"} &&
                    !custom.settings.lock_before_sleep,
                "power not parsed");
        require(shaodesk::parse_config("return {power={lock_command={}}}").power.lock_command.empty(),
                "an empty lock_command is not none");
        rejects("return {power={lock_command='swaylock -f'}}");
        rejects("return {power={lock_command={''}}}");
        rejects("return {power={lock_command={'swaylock',1}}}");
        rejects("return {power={lock_command={[2]='swaylock'}}}");
        rejects("return {power={locker={'swaylock'}}}");
        rejects("return {power=true}");
        rejects("return {power={lock_before_sleep='yes'}}");
        std::cout << "power configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
