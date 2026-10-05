// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the keyboard: XKB names and rules, and keymap files.
#include "shaodesk/config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

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

// A directory of its own for configuration and keymap files, removed afterwards.
struct Scratch {
    fs::path root;
    Scratch() {
        auto pattern = (fs::temp_directory_path() / "shaodesk-keyboard-XXXXXX").string();
        if (!mkdtemp(pattern.data()))
            throw std::runtime_error("cannot make a temporary directory");
        root = pattern;
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    fs::path write(const std::string &name, const std::string &text) const {
        auto path = root / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        return path;
    }
};
// A keymap with two layouts, the first renamed so a test can tell it came from the file.
const std::string good_keymap = "xkb_keymap {\n"
                                "  xkb_keycodes { include \"evdev\" };\n"
                                "  xkb_types { include \"complete\" };\n"
                                "  xkb_compat { include \"complete\" };\n"
                                "  xkb_symbols { include \"pc+us+no:2\" name[Group1] = \"Testish\"; };\n"
                                "};\n";
// The same with a syntax error on line 5, column 43.
const std::string broken_keymap = "xkb_keymap {\n"
                                  "  xkb_keycodes { include \"evdev\" };\n"
                                  "  xkb_types { include \"complete\" };\n"
                                  "  xkb_compat { include \"complete\" };\n"
                                  "  xkb_symbols { include \"pc+us+no:2\" oops };\n"
                                  "};\n";
// `keyboard = { file = FILE }` on line 3 of an init.lua.
std::string with_file(const std::string &file) {
    return "return {\n  keyboard = {\n    file = \"" + file + "\",\n    layout = \"no\",\n  },\n}\n";
}

static void keymap_files() {
    Scratch scratch;
    auto keymap = scratch.write("keymap.xkb", good_keymap);
    // Relative to the configuration file, wherever shaodesk runs from.
    auto init = scratch.write("init.lua", with_file("keymap.xkb"));
    auto config = shaodesk::load_config(init);
    require(fs::path(config.settings.keyboard_file) == keymap,
            std::string("keyboard.file not made absolute: ") + config.settings.keyboard_file);
    require(std::string(config.settings.keyboard_layout) == "no",
            "the names stay set beside a file, to stand in for it");
    init = scratch.write("nested/init.lua", with_file("../keymaps/../keymap.xkb"));
    require(fs::path(shaodesk::load_config(init).settings.keyboard_file) == keymap,
            "a relative path is not taken from the configuration's directory");
    init = scratch.write("init.lua", with_file(keymap.string()));
    require(fs::path(shaodesk::load_config(init).settings.keyboard_file) == keymap,
            "an absolute path changed");
    const char *home = std::getenv("HOME");
    const std::string saved = home ? home : "";
    setenv("HOME", scratch.root.c_str(), 1);
    init = scratch.write("init.lua", with_file("~/keymap.xkb"));
    require(fs::path(shaodesk::load_config(init).settings.keyboard_file) == keymap,
            "~/ is not the home directory");
    unsetenv("HOME");
    try {
        (void)shaodesk::load_config(init);
        throw std::runtime_error("~/ without HOME was accepted");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()).find("HOME is not set") != std::string::npos,
                std::string("~/ without HOME: ") + error.what());
    }
    if (home)
        setenv("HOME", saved.c_str(), 1);
    require(std::string(shaodesk::parse_config("return {keyboard={file=''}}").settings.keyboard_file)
                .empty(),
            "an empty keyboard.file is not unset");
    rejects("return {keyboard={file=3}}", "keyboard.file must be a string");

    // What is wrong, with the line of the setting and, where xkbcommon knows it, of the keymap.
    auto fails = [&](const std::string &file, const std::string &text) {
        auto path = scratch.root / "init.lua";
        std::ofstream(path, std::ios::binary | std::ios::trunc) << with_file(file);
        try {
            (void)shaodesk::load_config(path);
        } catch (const std::exception &error) {
            std::string message = error.what();
            require(message.find("init.lua:3: keyboard.file: ") != std::string::npos &&
                        message.find(text) != std::string::npos,
                    "expected '" + text + "' at init.lua:3 in: " + message);
            return message;
        }
        throw std::runtime_error("keyboard.file " + file + " was accepted");
    };
    fails("missing.xkb", "cannot read " + (scratch.root / "missing.xkb").string() +
                             ": No such file or directory");
    auto broken = scratch.write("broken.xkb", broken_keymap);
    fails("broken.xkb", broken.string() + ":5:43: syntax error");
    scratch.write("include.xkb", "xkb_keymap {\n  xkb_keycodes { include \"evdev\" };\n"
                                 "  xkb_types { include \"complete\" };\n"
                                 "  xkb_compat { include \"complete\" };\n"
                                 "  xkb_symbols { include \"pc+zz-no-such\" };\n};\n");
    fails("include.xkb", (scratch.root / "include.xkb").string() +
                             ": Couldn't find file \"symbols/zz-no-such\"");
    scratch.write("empty.xkb", "");
    fails("empty.xkb", (scratch.root / "empty.xkb").string() + ": ");
    fails(".", "cannot read");

    // A session starting with it, or reloading it, gets the default configuration instead,
    // whose keymap comes from the names, and the red bar says what is wrong and where.
    auto init_path = scratch.write("init.lua", with_file("broken.xkb"));
    std::string error;
    auto fallback = shaodesk::load_config_or_default(init_path, error);
    require(std::string(fallback.settings.keyboard_file).empty() &&
                !std::string(fallback.settings.keyboard_layout).empty(),
            "a broken keymap file did not leave the default keyboard settings");
    require(error.find("init.lua:3: keyboard.file: " + broken.string() + ":5:43") !=
                std::string::npos,
            "the fallback's error lacks the file and line: " + error);
}

int main() {
    try {
        rules();
        diagnostics();
        keymap_files();
        std::cout << "Keyboard configuration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
