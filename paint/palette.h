#pragma once

#include <mcx/chunk.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mcx {

struct PaletteEntry {
    BlockState state;
    std::uint32_t colour;
    std::uint8_t r, g, b;
};

class Palette {
public:
    static const Palette& get();

    BlockState nearest(std::uint8_t r, std::uint8_t g, std::uint8_t b) const;
    BlockState nearest(std::uint32_t rgb) const;
    std::uint32_t colour(BlockState state) const;

private:
    Palette();
    std::vector<PaletteEntry> entries;
    mutable std::unordered_map<std::uint32_t, BlockState> cache;
    mutable std::mutex cacheLock;
};

}
