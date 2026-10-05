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
        const auto &d = defaults.settings;
        require(defaults.power.lock_command == shaodesk::Command{"swaylock", "-f"} &&
                    d.lock_before_sleep && d.close_windows && d.close_timeout == 5000 &&
                    !d.close_force,
                "power defaults");
        auto custom = shaodesk::parse_config(
            "return {power={lock_command={'gtklock','--daemonize'},lock_before_sleep=false,"
            "close_windows=false,close_timeout=500,force=true}}");
        const auto &c = custom.settings;
        require(custom.power.lock_command == shaodesk::Command{"gtklock", "--daemonize"} &&
                    !c.lock_before_sleep && !c.close_windows && c.close_timeout == 500 &&
                    c.close_force,
                "power not parsed");
        require(shaodesk::parse_config("return {power={lock_command={}}}").power.lock_command.empty(),
                "an empty lock_command is not none");
        rejects("return {power={lock_command='swaylock -f'}}");
        rejects("return {power={lock_command={''}}}");
        rejects("return {power={lock_command={'swaylock',1}}}");
        rejects("return {power={lock_command={[2]='swaylock'}}}");
        rejects("return {power={locker={'swaylock'}}}");
        require(defaults.shell.widgets.power &&
                    !shaodesk::parse_config("return {shell={widgets={power=false}}}")
                         .shell.widgets.power,
                "shell.widgets.power not parsed");
        rejects("return {shell={widgets={power='no'}}}");
        rejects("return {power=true}");
        rejects("return {power={lock_before_sleep='yes'}}");
        rejects("return {power={close_windows=1}}");
        rejects("return {power={close_timeout=499}}");
        rejects("return {power={close_timeout=60001}}");
        rejects("return {power={close_timeout=1.5}}");
        rejects("return {power={force='no'}}");
        require(defaults.power.countdown == 10 &&
                    shaodesk::parse_config("return {power={countdown=0}}").power.countdown == 0 &&
                    shaodesk::parse_config("return {power={countdown=300}}").power.countdown ==
                        300,
                "power.countdown not parsed");
        rejects("return {power={countdown=-1}}");
        rejects("return {power={countdown=301}}");
        rejects("return {power={countdown='10'}}");
        std::cout << "power configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
