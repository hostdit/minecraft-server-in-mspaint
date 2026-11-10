#include "palette.h"

#include <mcx/blocks.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace mcx {

namespace {

struct UniqueBlock {
    BlockState state;
    std::uint32_t colour;
};

std::vector<UniqueBlock> buildPalette() {
    std::set<std::uint32_t> seen;
    std::vector<UniqueBlock> result;

    auto add = [&](int id, int meta) {
        BlockState s = state(id, meta);
        std::uint32_t c = colourOf(s);
        if (c == 0 || !seen.insert(c).second) return;
        result.push_back({s, c});
    };

    add(1, 0);  add(1, 1);  add(1, 3);  add(1, 5);
    add(2, 0);  add(3, 0);  add(3, 2);
    add(4, 0);  add(7, 0);
    add(12, 0); add(12, 1); add(13, 0);
    add(14, 0); add(15, 0); add(16, 0);
    add(17, 0); add(17, 1); add(17, 2); add(17, 3);
    add(18, 0); add(19, 0);
    add(20, 0); add(21, 0); add(22, 0);
    add(24, 0); add(41, 0); add(42, 0);
    add(43, 0); add(45, 0); add(46, 0);
    add(47, 0); add(48, 0); add(49, 0);
    add(54, 0); add(56, 0); add(57, 0);
    add(58, 0); add(79, 0); add(80, 0);
    add(82, 0); add(86, 0); add(87, 0);
    add(88, 0); add(89, 0); add(98, 0);
    add(103, 0); add(110, 0); add(112, 0);
    add(121, 0); add(129, 0); add(133, 0);
    add(152, 0); add(153, 0);
    add(155, 0); add(159, 0);
    add(162, 0); add(162, 1);
    add(168, 0); add(168, 1); add(168, 2);
    add(170, 0); add(172, 0); add(173, 0);
    add(174, 0);

    for (int meta = 0; meta < 16; meta++) {
        add(35, meta);
        add(159, meta);
        add(95, meta);
    }

    add(5, 0); add(5, 1); add(5, 2); add(5, 3); add(5, 4); add(5, 5);

    return result;
}

}

const Palette& Palette::get() {
    static Palette* palette = new Palette();
    return *palette;
}

Palette::Palette() {
    auto blocks = buildPalette();
    for (auto& b : blocks) {
        entries.push_back({
            b.state, b.colour,
            std::uint8_t((b.colour >> 16) & 0xFF),
            std::uint8_t((b.colour >> 8) & 0xFF),
            std::uint8_t(b.colour & 0xFF)
        });
    }
}

BlockState Palette::nearest(std::uint8_t r, std::uint8_t g, std::uint8_t b) const {
    std::uint32_t key = (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    {
        std::lock_guard guard(cacheLock);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
    }

    int bestDist = INT_MAX;
    BlockState bestState = state(2, 0);

    for (auto& entry : entries) {
        int dr = int(r) - int(entry.r);
        int dg = int(g) - int(entry.g);
        int db = int(b) - int(entry.b);
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestState = entry.state;
            if (dist == 0) break;
        }
    }

    {
        std::lock_guard guard(cacheLock);
        cache[key] = bestState;
    }
    return bestState;
}

BlockState Palette::nearest(std::uint32_t rgb) const {
    return nearest(
        std::uint8_t((rgb >> 16) & 0xFF),
        std::uint8_t((rgb >> 8) & 0xFF),
        std::uint8_t(rgb & 0xFF)
    );
}

std::uint32_t Palette::colour(BlockState s) const {
    return colourOf(s);
}

}
