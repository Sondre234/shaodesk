// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the keyboard: XKB names and rules.
#include "shaodesk/config.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
// The error `source` gives; fails the test when it is accepted.
static std::string error_of(const std::string &source) {
    try {
        (void)shaodesk::parse_config(source, "@init.lua");
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
static void rejects(const std::string &source, const std::string &fragment) {
    auto message = error_of(source);
    require(message.find(fragment) != std::string::npos,
            "expected '" + fragment + "' in: " + message);
}

static void rules() {
    auto defaults = shaodesk::parse_config("return {}");
    require(std::string(defaults.settings.keyboard_rules).empty(), "rules default to xkbcommon's");
    auto evdev = shaodesk::parse_config("return {keyboard={rules='evdev',layout='us'}}");
    require(std::string(evdev.settings.keyboard_rules) == "evdev", "keyboard.rules not parsed");
    rejects("return {keyboard={rules=3}}", "rules must be a string");
    rejects("return {keyboard={rules='no-such-rules-zz'}}", "keyboard");
}

// Names XKB has no keymap for are refused with its reason, at the keyboard table.
static void diagnostics() {
    rejects("return {\n  layout = { gap = 2 },\n  keyboard = {\n    layout = 'zz-no-such',\n  },\n}",
            "init.lua:3: XKB has no keymap");
    rejects("return {keyboard={layout='zz-no-such'}}", "symbols/zz-no-such");
    rejects("return {keyboard={layout='us',variant='zz-no-such'}}", "zz-no-such");
}

int main() {
    try {
        rules();
        diagnostics();
        std::cout << "Keyboard configuration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
