#pragma once

#include "paintui.h"
#include "palette.h"

#include <mcx/world.h>

#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace mcx {

constexpr int surfaceY = 3;
constexpr int canvasSize = (worldRadius * 2 + 1) * 16;
constexpr int canvasMinBlock = -worldRadius * 16;

class Canvas {
public:
    explicit Canvas(World& world);

    bool tick();
    void blockChanged(const Change& change);

private:
    using Clock = std::chrono::steady_clock;

    World& world;
    const Palette& palette;
    PaintUi ui;
    HWND paintWnd = nullptr;
    HWND viewWnd = nullptr;
    std::optional<CanvasGeometry> lastGeometry;
    std::vector<std::uint32_t> committed;
    std::vector<std::uint32_t> previous;
    std::vector<Clock::time_point> echoUntil;
    std::map<int, int> brushQueue;
    int baselineFrames = 0;
    int sampledFrames = 0;
    int settleTicks = 0;
    bool paused = false;
    std::mutex brushLock;

    bool attach();
    POINT pointFor(const CanvasGeometry& geometry, int px, int py) const;
    void paintQueued(const CanvasGeometry& geometry);
    void sample(const CanvasGeometry& geometry, const std::vector<std::uint32_t>& pixels, int width, int height, POINT view);
    void applyPixel(int px, int py, std::uint32_t colour);
    int columnSwatch(int x, int z) const;
    bool isPaintAt(POINT point) const;
    std::vector<std::uint32_t> capture(int& width, int& height) const;
};

}
