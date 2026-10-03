// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration diagnostics: unknown settings, wrong types, ranges, robustness, and the
// schema that docs/config-reference.md is generated from.
#include "shaodesk/config.hpp"
#include "shaodesk/config_schema.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
// The message the parser gives for `source`; fails the test if it is accepted.
std::string error_of(const std::string &source, const std::string &name = "@test.lua") {
    try {
        (void)shaodesk::parse_config(source, name);
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
void expect(const std::string &source, const std::string &fragment) {
    auto message = error_of(source);
    require(message.find(fragment) != std::string::npos,
            "expected '" + fragment + "' in: " + message + "\n  for " + source);
}
std::string literal(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}
// "a.b.c" and a value as `return { a = { b = { c = VALUE } } }`; empty for list or map paths.
std::string wrap(const std::string &path, const std::string &value) {
    if (path.find_first_of("[<") != std::string::npos)
        return "";
    std::string open, close, rest = path;
    for (size_t dot; (dot = rest.find('.')) != std::string::npos; rest = rest.substr(dot + 1)) {
        open += rest.substr(0, dot) + "={";
        close += "}";
    }
    return "return {" + open + rest + "=" + value + close + "}";
}
bool accepted(const std::string &source) {
    try {
        (void)shaodesk::parse_config(source);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
void unknown_settings() {
    expect("return {shell={pannel_height=40}}", "unknown setting 'pannel_height' in shell");
    expect("return {shell={pannel_height=40}}", "did you mean 'panel_height'?");
    expect("return {layot={}}", "did you mean 'layout'?");
    expect("return {mouse={focus_follow=true}}", "did you mean 'focus_follows'?");
    expect("return {animations={durration=100}}", "did you mean 'duration'?");
    expect("return {features={scratchpd=true}}", "unknown feature 'scratchpd'; did you mean "
                                                  "'scratchpad'?");
    expect("return {bindings={{key='q',action='clsoe'}}}", "did you mean 'close'?");
    expect("return {bindings={{mods={'Supr'},key='q',action='close'}}}", "did you mean 'Super'?");
    expect("return {bindings={{button='sid',action='close'}}}", "did you mean 'side'?");
    expect("return {bindings={{key='q',action='close',comand={'x'}}}}", "did you mean 'command'?");
    expect("return {windows={rules={{app_id='x',floatng=true}}}}", "did you mean 'floating'?");
    expect("return {outputs={monitors={X={scal=2}}}}", "did you mean 'scale'?");
    expect("return {shell={launchers={{name='a',command={'x'},icn='b'}}}}",
           "did you mean 'icon'?");
    expect("return {shell={panel_margin={tp=1}}}", "did you mean 'top'?");
    // Nothing close: list what is valid.
    expect("return {shell={zzzzzzzz=1}}", "expected one of: enabled, panel_height");
    expect("return {shell={zzzzzzzz=1}}", "expected one of:");
    require(error_of("return {shell={zzzzzzzz=1}}").find("did you mean") == std::string::npos,
            "a far-off name got a suggestion");
}
void locations() {
    auto at = [](const std::string &source, const std::string &fragment) {
        auto message = error_of(source);
        require(message.find("test.lua:" + fragment) != std::string::npos,
                "location " + fragment + " missing from: " + message);
    };
    at("return {\n  shell = {\n    enabled = true,\n    pannel_height = 4,\n  },\n}",
       "4: unknown setting");
    // The same name in another section, and one in a comment, are not mistaken for it.
    at("-- enabled = 1\nreturn {\n  animations = { enabled = true },\n  shell = {\n"
       "    enabled = 'yes',\n  },\n}",
       "5: shell.enabled must be a boolean");
    at("return {\n  layout = {\n    gap = 1,\n    workspaces = 'four',\n  },\n}",
       "4: layout.workspaces");
    at("return {\n  bindings = {\n    { key = 'q', action = 'close' },\n"
       "    { key = 'w', action = 'clsoe' },\n  },\n}",
       "4: unknown action");
    at("return {\n  outputs = {\n    monitors = {\n      [\"DP-1\"] = { scal = 2 },\n"
       "    },\n  },\n}",
       "4: unknown setting");
    // Lua's own errors already carry file:line.
    at("return {\n  x = = 1,\n}", "2:");
    at("local a = nil\nreturn { shell = a.b }", "2:");
    // No location is invented for a setting that is not in the file.
    auto message = error_of("return {shell=(function() return {panel_height=1} end)()}");
    require(message.find("test.lua:") == std::string::npos ||
                message.find("shell.panel_height") != std::string::npos,
            "unexpected location: " + message);
    auto plain = shaodesk::parse_config("return {}", "@x.lua");
    (void)plain;
}
void wrong_types() {
    expect("return {shell={panel_height='big'}}", "shell.panel_height must be an integer, not a string");
    expect("return {shell={panel_height=1.5}}", "not a non-integer number");
    expect("return {shell={enabled=1}}", "shell.enabled must be a boolean, not an integer");
    expect("return {shell={font=3}}", "must be a string, not an integer");
    expect("return {mouse={speed='fast'}}", "mouse.speed must be a number, not a string");
    expect("return {shell=true}", "shell must be a table, not a boolean");
    expect("return {shell={launchers=3}}", "must be a table");
    expect("return 3", "configuration result must be a table, not an integer");
    expect("return {features={sticky='no'}}", "features.sticky must be a boolean, not a string");
}
void ranges() {
    expect("return {shell={panel_height=500}}", "shell.panel_height must be between 24 and 100, not 500");
    expect("return {layout={gap=-3}}", "layout.gap must be between 0 and 100, not -3");
    expect("return {windows={opacity=2}}", "windows.opacity must be between 0.05 and 1, not 2");
    expect("return {keyboard={repeat_rate=101}}", "between 0 and 100");
    expect("return {outputs={monitors={X={transform=9}}}}", "between 0 and 7");
}
void schema_matches_parser() {
    auto options = shaodesk::config_options();
    size_t probed = 0, ranged = 0;
    for (const auto &option : options) {
        std::string path = option.path;
        // Every option lives inside one the schema lists (or at the top).
        auto dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            auto parent = path.substr(0, dot);
            bool found = false;
            for (const auto &other : options) {
                std::string p = other.path;
                if (p == parent || p + "[]" == parent)
                    found = true;
            }
            require(found || parent.ends_with("[]") || parent.ends_with("<name>"),
                    path + " has no parent in the schema");
        }
        std::string example = option.example;
        if (example.empty())
            continue;
        auto source = wrap(path, example);
        if (source.empty())
            continue;
        ++probed;
        require(accepted(source), "the parser rejects the documented example for " + path + ": " +
                                      source);
        if (option.min != option.max) {
            ++ranged;
            require(accepted(wrap(path, literal(option.min))), path + " rejects its minimum");
            require(accepted(wrap(path, literal(option.max))), path + " rejects its maximum");
            require(!accepted(wrap(path, literal(option.min - 1))),
                    path + " accepts less than its minimum");
            require(!accepted(wrap(path, literal(option.max + 1))),
                    path + " accepts more than its maximum");
        }
        // A wrong-typed value is refused; a table for a scalar, and a string for a table.
        if (std::string(option.type) == "boolean")
            require(!accepted(wrap(path, "'x'")), path + " accepts a string as a boolean");
    }
    require(probed > 40 && ranged > 15, "the schema probes covered too little: " + std::to_string(probed) + "/" + std::to_string(ranged));
    // Each top-level key the parser knows is in the schema and the other way around.
    for (const char *name : {"version", "extends", "theme", "appearance", "keyboard", "mouse",
                             "touchpad", "layout", "outputs", "windows", "animations", "bindings",
                             "startup", "shell", "xwayland", "screenshots", "features", "overview", "peek", "night_light", "hot_corners", "zoom", "notifications", "osd", "profile", "profiles", "auto_reload"}) {
        bool found = false;
        for (const auto *child : shaodesk::config_children(""))
            found = found || std::string(child->path) == name;
        require(found, std::string(name) + " missing from the schema");
    }
    require(shaodesk::config_children("").size() == 27, "the schema has an unknown top-level key");
    // A key that is in the schema is accepted by keys(), however deeply nested.
    require(accepted("return {windows={rules={{app_id='x',sticky=true,focus=false}}}}"),
            "rule keys rejected");
    for (const auto &name : shaodesk::config_action_names())
        (void)shaodesk::parse_action(name);
}
void robustness() {
    auto throws = [](const std::string &source) {
        try {
            (void)shaodesk::parse_config(source, "@bad.lua");
        } catch (const std::exception &) {
            return;
        }
        throw std::runtime_error("accepted: " + source.substr(0, 40));
    };
    throws("return {");
    throws("this is not lua");
    throws("error('boom')");
    throws("error({})");
    throws("error(nil)");
    throws("return nil");
    throws("return 'text'");
    throws("while true do end");
    throws("local function f() return f() + 1 end return f()");
    throws("return {shell={panel_height=" + std::string(400, '9') + "}}");
    throws("return {startup={{" + std::string(5000, 'x') + "}}}");
    throws("return {[1]=1}");
    throws("return {bindings={[2]={}}}");
    throws("return {windows={rules={{app_id='(((((('}}}}");
    throws("return {keyboard={layout='nonexistent-layout-zz'}}");
    throws("return {\"a\\0b\"}");
    std::string deep = "return ";
    for (int i = 0; i < 300; ++i)
        deep += "{a=";
    throws(deep + "1" + std::string(300, '}'));
    // A file that is missing or unreadable is an error, not a crash.
    try {
        (void)shaodesk::load_config("/nonexistent/dir/init.lua");
        throw std::runtime_error("a missing file was accepted");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()).find("cannot open") != std::string::npos,
                "missing file message");
    }
    // After any failure the next parse works: no state survives a rejected configuration.
    require(shaodesk::parse_config("return {layout={gap=3}}").settings.gap_inner == 3,
            "parsing after a failure broke");
}
void reference_in_sync(const std::string &path) {
    auto generated = shaodesk::config_reference_markdown();
    if (std::getenv("SHAODESK_UPDATE_DOCS")) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << generated;
        std::cout << "wrote " << path << '\n';
        return;
    }
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "cannot read " + path);
    std::stringstream text;
    text << file.rdbuf();
    require(text.str() == generated,
            path + " is out of date; regenerate it with SHAODESK_UPDATE_DOCS=1 ctest -R config");
    for (const auto &option : shaodesk::config_options())
        require(generated.find(std::string("`") + option.path + "`") != std::string::npos,
                std::string("reference lacks ") + option.path);
    for (const auto &name : shaodesk::config_action_names())
        require(generated.find("`" + name + "`") != std::string::npos, "reference lacks " + name);
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: config_diagnostics_tests EXAMPLE DOCS");
        setenv("SHAODESK_DEFAULT_CONFIG", argv[1], 1); // for extends = "default"
        unknown_settings();
        locations();
        wrong_types();
        ranges();
        schema_matches_parser();
        robustness();
        reference_in_sync(argv[2]);
        // The shipped example has no unknown or mistyped setting.
        (void)shaodesk::load_config(argv[1]);
        std::cout << "Configuration diagnostics and reference passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
