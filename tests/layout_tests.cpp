// SPDX-License-Identifier: GPL-3.0-or-later
#include "shaodesk/backend.h"
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value) {
    if (!value)
        throw std::runtime_error("layout invariant failed");
}
int main() {
    try {
        const sh_rect area{-1920, 20, 1919, 1079};
        for (int count = 1; count < 64; ++count) {
            std::vector<sh_rect> placed;
            for (int i = 0; i < count; ++i) {
                sh_rect rect;
                require(sh_placement(SH_TILE, area, 8, i, count, &rect));
                require(rect.width > 0 && rect.height > 0);
                require(rect.x >= area.x && rect.y >= area.y);
                require(rect.x + rect.width <= area.x + area.width);
                require(rect.y + rect.height <= area.y + area.height);
                for (const auto &other : placed)
                    require(rect.x + rect.width <= other.x || other.x + other.width <= rect.x ||
                            rect.y + rect.height <= other.y || other.y + other.height <= rect.y);
                placed.push_back(rect);
            }
        }
        sh_rect left, right;
        require(sh_placement(SH_SNAP_LEFT, area, 8, 0, 1, &left));
        require(sh_placement(SH_SNAP_RIGHT, area, 8, 0, 1, &right));
        require(left.x + left.width + 8 == right.x);
        require(right.x + right.width + 8 == area.x + area.width);
        // The quarters share the halves' columns and split them into rows, with the gap
        // between and around them.
        sh_rect quarters[4];
        const sh_action corners[] = {SH_SNAP_TOP_LEFT, SH_SNAP_TOP_RIGHT, SH_SNAP_BOTTOM_LEFT,
                                     SH_SNAP_BOTTOM_RIGHT};
        for (int i = 0; i < 4; ++i)
            require(sh_placement(corners[i], area, 8, 0, 1, &quarters[i]));
        require(quarters[0].x == left.x && quarters[0].width == left.width);
        require(quarters[1].x == right.x && quarters[1].width == right.width);
        require(quarters[0].y == left.y && quarters[2].x == left.x);
        require(quarters[0].y + quarters[0].height + 8 == quarters[2].y);
        require(quarters[2].y + quarters[2].height == left.y + left.height);
        require(quarters[3].y == quarters[2].y && quarters[3].height == quarters[2].height);
        require(quarters[1].height == quarters[0].height);
        require(!sh_placement(SH_SNAP_TOP_LEFT, {0, 0, 2, 1}, 0, 0, 1, &left));
        require(sh_placement(SH_TILE, {0, 0, 2, 2}, 100, 3, 4, &left));
        require(left.width == 1 && left.height == 1);
        require(!sh_placement(SH_TILE, {0, 0, 1, 1}, 0, 1, 2, &left));
        require(!sh_placement(SH_TILE, area, 8, 0, 0, &left));

        // Snap zones of a 1920 x 1080 output with a 40-pixel panel along the top: within 8
        // pixels of a side, of the top (the panel included), or of two sides at once.
        const sh_rect usable{0, 40, 1920, 1040};
        auto zone = [&](double x, double y, bool corners = true, uint32_t shared = 0) {
            return sh_snap_zone(usable, x, y, 8, corners, shared);
        };
        require(zone(960, 500) == SH_NONE);
        require(zone(0, 500) == SH_SNAP_LEFT && zone(7.9, 500) == SH_SNAP_LEFT);
        require(zone(8, 500) == SH_NONE);
        require(zone(1919, 500) == SH_SNAP_RIGHT && zone(1912, 500) == SH_SNAP_RIGHT);
        require(zone(1911.5, 500) == SH_NONE);
        require(zone(960, 0) == SH_MAXIMIZE && zone(960, 47) == SH_MAXIMIZE);
        require(zone(960, 48) == SH_NONE);
        require(zone(960, 1079) == SH_NONE); // the bottom alone
        require(zone(0, 0) == SH_SNAP_TOP_LEFT && zone(3, 45) == SH_SNAP_TOP_LEFT);
        require(zone(1919, 10) == SH_SNAP_TOP_RIGHT);
        require(zone(0, 1079) == SH_SNAP_BOTTOM_LEFT && zone(1919, 1075) == SH_SNAP_BOTTOM_RIGHT);
        require(zone(0, 1060) == SH_SNAP_LEFT); // 19 pixels above the bottom
        // Without corners the side wins; a shared edge snaps nothing, and leaves the other.
        require(zone(0, 0, false) == SH_SNAP_LEFT && zone(1919, 1079, false) == SH_SNAP_RIGHT);
        require(zone(0, 500, true, SH_EDGE_LEFT) == SH_NONE);
        require(zone(0, 0, true, SH_EDGE_LEFT) == SH_MAXIMIZE);
        require(zone(0, 0, true, SH_EDGE_TOP) == SH_SNAP_LEFT);
        require(zone(960, 0, true, SH_EDGE_TOP) == SH_NONE);
        require(zone(0, 1079, true, SH_EDGE_BOTTOM) == SH_SNAP_LEFT);
        require(zone(1919, 500, true, SH_EDGE_LEFT) == SH_SNAP_RIGHT);
        require(sh_snap_zone(usable, 0, 500, 0, true, 0) == SH_NONE);
        require(sh_snap_zone({0, 0, 0, 0}, 0, 0, 8, true, 0) == SH_NONE);
        std::cout << "Tiling bounds, gaps, non-overlap and snap zones passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
