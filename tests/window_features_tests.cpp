// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of windows.swallow (and the other window-management settings of `windows`).
#include "shaode/config.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void rejects(const std::string &source) {
    try {
        (void)shaode::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}

int main() {
    try {
        // Swallowing is off by default and knows the common terminals.
        auto defaults = shaode::parse_config("return {}");
        require(!defaults.settings.swallow && defaults.settings.swallow_terminal_count == 9 &&
                    !std::strcmp(defaults.settings.swallow_terminals[0], "kitty") &&
                    defaults.settings.swallow_exception_count == 0,
                "swallow defaults changed");
        auto swallow = shaode::parse_config(
            "return {windows={swallow={enabled=true,terminals={'foo','Bar'},exceptions={'baz'}}}}");
        require(swallow.settings.swallow && swallow.settings.swallow_terminal_count == 2 &&
                    !std::strcmp(swallow.settings.swallow_terminals[1], "Bar") &&
                    swallow.settings.swallow_exception_count == 1 &&
                    !std::strcmp(swallow.settings.swallow_exceptions[0], "baz"),
                "swallow settings not read");
        auto none = shaode::parse_config("return {windows={swallow={terminals={}}}}");
        require(!none.settings.swallow && none.settings.swallow_terminal_count == 0,
                "an empty terminal list should clear the defaults");
        require(shaode::parse_config("return {windows={swallow={enabled=true}}}")
                        .settings.swallow_terminal_count == 9,
                "enabling keeps the default terminals");
        rejects("return {windows={swallow={enabled='yes'}}}");
        rejects("return {windows={swallow={terminals='kitty'}}}");
        rejects("return {windows={swallow={terminals={1}}}}");
        rejects("return {windows={swallow={terminal={'kitty'}}}}");
        rejects("return {windows={swallow=true}}");
        std::string many = "return {windows={swallow={terminals={";
        for (int i = 0; i < 33; ++i)
            many += "'t" + std::to_string(i) + "',";
        rejects(many + "}}}}");
        rejects("return {windows={swallow={exceptions={'" + std::string(70, 'x') + "'}}}}");
        require(!shaode::parse_config("return {bindings={{mods={'Super'},key='w',"
                                      "action='swallow_toggle'}}}").bindings.empty(),
                "swallow_toggle not accepted");
        // Magnetic edges are on, 12 pixels, with guides and Shift to bypass.
        require(defaults.settings.magnet && defaults.settings.magnet_distance == 12 &&
                    defaults.settings.magnet_guides && defaults.settings.magnet_bypass == SH_SHIFT &&
                    defaults.settings.magnet_guide_color[3] > 0.69F &&
                    defaults.settings.magnet_guide_color[3] < 0.71F,
                "magnet defaults changed");
        auto magnet = shaode::parse_config(
            "return {windows={magnet={enabled=false,distance=30,guides=false,bypass='Super',"
            "guide_color='#ff000080'}}}");
        require(!magnet.settings.magnet && magnet.settings.magnet_distance == 30 &&
                    !magnet.settings.magnet_guides && magnet.settings.magnet_bypass == SH_LOGO &&
                    magnet.settings.magnet_guide_color[0] > 0.49F &&
                    magnet.settings.magnet_guide_color[0] < 0.51F,
                "magnet settings not read (premultiplied color)");
        require(shaode::parse_config("return {windows={magnet={bypass='none'}}}")
                        .settings.magnet_bypass == 0,
                "bypass = none");
        require(shaode::parse_config("return {windows={magnet={distance=0}}}")
                        .settings.magnet_distance == 0 &&
                    shaode::parse_config("return {windows={magnet={distance=200}}}")
                            .settings.magnet_distance == 200,
                "distance limits");
        rejects("return {windows={magnet={distance=-1}}}");
        rejects("return {windows={magnet={distance=201}}}");
        rejects("return {windows={magnet={distance=1.5}}}");
        rejects("return {windows={magnet={enabled=1}}}");
        rejects("return {windows={magnet={guides='yes'}}}");
        rejects("return {windows={magnet={bypass='Hyper'}}}");
        rejects("return {windows={magnet={guide_color='blue'}}}");
        rejects("return {windows={magnet={dist=3}}}");
        rejects("return {windows={magnet=false}}");
        // windows.placement: cascade unless told otherwise.
        require(defaults.settings.placement == SH_PLACE_CASCADE, "placement default");
        for (auto [name, mode] : {std::pair{"cascade", SH_PLACE_CASCADE},
                                  {"center", SH_PLACE_CENTER}, {"smart", SH_PLACE_SMART}}) {
            require(shaode::parse_config(std::string("return {windows={placement='") + name + "'}}")
                            .settings.placement == mode,
                    "placement not read");
        }
        rejects("return {windows={placement='random'}}");
        rejects("return {windows={placement=1}}");
        rejects("return {windows={placement=true}}");
        std::cout << "Window feature configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
