#include "canvas.h"

#include "common.h"

#include <mcx/blocks.h>

#include <algorithm>
#include <climits>

namespace mcx {

namespace {

constexpr std::uint32_t unknownPixel = 0xFFFFFFFF;
constexpr std::size_t dabsPerTick = 48;
constexpr auto echoTime = std::chrono::milliseconds(1500);
constexpr int whiteSwatch = 10;
constexpr int settleNeeded = 3;
constexpr int penInset = 1;
constexpr double minimumScale = 3;

struct Swatch {
    const wchar_t* name;
    std::uint32_t colour;
};

constexpr Swatch swatches[] = {
    {L"Black", 0x000000}, {L"Grey", 0x7F7F7F}, {L"Dark red", 0x880015}, {L"Red", 0xED1C24}, {L"Orange", 0xFF7F27},
    {L"Yellow", 0xFFF200}, {L"Green", 0x22B14C}, {L"Turquoise", 0x00A2E8}, {L"Indigo", 0x3F48CC}, {L"Purple", 0xA349A4},
    {L"White", 0xFFFFFF}, {L"Light grey", 0xC3C3C3}, {L"Brown", 0xB97A57}, {L"Rose", 0xFFAEC9}, {L"Gold", 0xFFC90E},
    {L"Light yellow", 0xEFE4B0}, {L"Lime", 0xB5E61D}, {L"Light turquoise", 0x99D9EA}, {L"Blue-grey", 0x7092BE}, {L"Lavender", 0xC8BFE7},
};

int nearestSwatch(std::uint32_t colour) {
    int best = whiteSwatch;
    int bestDistance = INT_MAX;
    for (int i = 0; i < int(std::size(swatches)); i++) {
        int red = int((colour >> 16) & 0xFF) - int((swatches[i].colour >> 16) & 0xFF);
        int green = int((colour >> 8) & 0xFF) - int((swatches[i].colour >> 8) & 0xFF);
        int blue = int(colour & 0xFF) - int(swatches[i].colour & 0xFF);
        int distance = red * red + green * green + blue * blue;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

bool isBlank(std::uint32_t colour) {
    return ((colour >> 16) & 0xFF) >= 250 && ((colour >> 8) & 0xFF) >= 250 && (colour & 0xFF) >= 250;
}

bool inView(const CanvasGeometry& geometry, POINT point) {
    return point.x >= geometry.viewLeft && point.x < geometry.viewRight && point.y >= geometry.viewTop && point.y < geometry.viewBottom;
}

HWND childOfClass(HWND parent, const wchar_t* name) {
    struct Search {
        const wchar_t* name;
        HWND found;
    } search{name, nullptr};
    EnumChildWindows(parent, [](HWND hwnd, LPARAM value) -> BOOL {
        auto* current = reinterpret_cast<Search*>(value);
        wchar_t found[64];
        GetClassNameW(hwnd, found, 64);
        if (_wcsicmp(found, current->name)) return TRUE;
        current->found = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

HWND paintWindow() {
    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        wchar_t name[64];
        GetClassNameW(hwnd, name, 64);
        if (pid != GetCurrentProcessId() || _wcsicmp(name, L"MSPaintApp")) return TRUE;
        *reinterpret_cast<HWND*>(value) = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

}

Canvas::Canvas(World& target)
    : world(target), palette(Palette::get()), committed(canvasSize * canvasSize, unknownPixel), previous(canvasSize * canvasSize, unknownPixel), echoUntil(canvasSize * canvasSize) {}

bool Canvas::attach() {
    HWND paint = paintWindow();
    HWND view = paint ? childOfClass(paint, L"MSPaintView") : nullptr;
    if (!view) return false;
    ui.attach(paint);
    paintWnd = paint;
    viewWnd = view;
    log("attached to Paint's canvas");
    return true;
}

bool Canvas::tick() {
    if (!paintWnd) {
        attach();
        return true;
    }
    if (!IsWindow(paintWnd)) return false;
    if (IsIconic(paintWnd)) return true;

    auto geometry = ui.geometry();
    if (!geometry) return true;
    if (geometry != lastGeometry) {
        if (geometry->whole && (!lastGeometry || lastGeometry->width != geometry->width || lastGeometry->height != geometry->height || lastGeometry->scale != geometry->scale))
            log(std::format("canvas {}x{} at {:.0f}%", geometry->width, geometry->height, geometry->scale * 100));
        if (sampledFrames >= 2) baselineFrames = 3;
        lastGeometry = geometry;
        settleTicks = 0;
        std::fill(previous.begin(), previous.end(), unknownPixel);
        if (geometry->whole && geometry->scale < minimumScale) {
            ui.setZoom(minimumScale * 100);
            return true;
        }
    }
    if (!geometry->whole) {
        if (!paused) log(std::format("canvas doesn't fit the view, maximise Paint or zoom out (shown {}x{})", geometry->shownWidth, geometry->shownHeight));
        paused = true;
        return true;
    }
    paused = false;
    int width, height;
    auto pixels = capture(width, height);
    if (pixels.empty()) return true;
    POINT view{};
    ClientToScreen(viewWnd, &view);
    if (++settleTicks >= settleNeeded) paintQueued(*geometry);
    sample(*geometry, pixels, width, height, view);
    return true;
}

POINT Canvas::pointFor(const CanvasGeometry& geometry, int px, int py) const {
    return {LONG(geometry.left + (px + 0.5) * geometry.scale), LONG(geometry.top + (py + 0.5) * geometry.scale)};
}

void Canvas::blockChanged(const Change& change) {
    if (change.author == "Paint" || change.kind == ChangeKind::chunkCleared) return;
    int px = change.position.x - canvasMinBlock;
    int py = change.position.z - canvasMinBlock;
    if (px < 0 || px >= canvasSize || py < 0 || py >= canvasSize) return;
    int swatch = columnSwatch(change.position.x, change.position.z);
    std::lock_guard guard(brushLock);
    int index = py * canvasSize + px;
    brushQueue[index] = swatch;
    echoUntil[index] = Clock::time_point::max();
}

int Canvas::columnSwatch(int x, int z) const {
    for (int y = worldHeight - 1; y >= surfaceY; y--) {
        BlockState block = world.get({x, y, z});
        if (!block) continue;
        if (y == surfaceY && block == state(2, 0)) return whiteSwatch;
        return nearestSwatch(colourOf(block));
    }
    return whiteSwatch;
}

bool Canvas::isPaintAt(POINT point) const {
    return GetAncestor(WindowFromPoint(point), GA_ROOT) == paintWnd;
}

void Canvas::paintQueued(const CanvasGeometry& geometry) {
    std::vector<std::pair<int, int>> batch;
    {
        std::lock_guard guard(brushLock);
        for (auto& [index, swatch] : brushQueue) {
            if (batch.size() == dabsPerTick) break;
            batch.push_back({swatch, index});
        }
    }
    if (batch.empty() || !ui.usePencil()) return;
    std::sort(batch.begin(), batch.end());

    auto finish = [&](int index, int swatch, Clock::time_point until) {
        std::lock_guard guard(brushLock);
        auto queued = brushQueue.find(index);
        if (queued != brushQueue.end() && queued->second == swatch) brushQueue.erase(queued);
        echoUntil[index] = until;
    };

    int picked = -1;
    for (auto& [swatch, index] : batch) {
        int px = index % canvasSize;
        int py = index / canvasSize;
        POINT point = pointFor(geometry, px, py);
        point.x += penInset;
        point.y += penInset;
        if (px >= geometry.width || py >= geometry.height || !inView(geometry, point)) {
            finish(index, swatch, Clock::now());
            continue;
        }
        if (!isPaintAt(point)) return;
        if (swatch != picked) {
            if (!ui.pickColour(swatches[swatch].name)) {
                log(std::format("couldn't pick colour {}", swatch));
                return;
            }
            picked = swatch;
            if (!isPaintAt(point)) return;
        }
        if (!ui.dab(point)) {
            log(std::format("pen injection failed {}", GetLastError()));
            return;
        }
        finish(index, swatch, Clock::now() + echoTime);
    }
}

std::vector<std::uint32_t> Canvas::capture(int& width, int& height) const {
    RECT client;
    GetClientRect(viewWnd, &client);
    width = client.right;
    height = client.bottom;
    if (width <= 0 || height <= 0) return {};

    HDC window = GetDC(viewWnd);
    if (!window) return {};
    HDC memory = CreateCompatibleDC(window);
    HBITMAP bitmap = CreateCompatibleBitmap(window, width, height);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BOOL printed = PrintWindow(viewWnd, memory, PW_RENDERFULLCONTENT);
    SelectObject(memory, old);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint32_t> pixels(std::size_t(width) * height);
    int rows = printed ? GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) : 0;
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(viewWnd, window);
    if (rows != height) return {};
    return pixels;
}

void Canvas::sample(const CanvasGeometry& geometry, const std::vector<std::uint32_t>& pixels, int width, int height, POINT view) {
    int columns = std::min(geometry.width, canvasSize);
    int rows = std::min(geometry.height, canvasSize);
    auto now = Clock::now();
    bool baseline = baselineFrames > 0;
    if (baseline) baselineFrames--;

    for (int py = 0; py < rows; py++) {
        for (int px = 0; px < columns; px++) {
            POINT point = pointFor(geometry, px, py);
            int x = point.x - view.x;
            int y = point.y - view.y;
            if (!inView(geometry, point) || x < 0 || x >= width || y < 0 || y >= height) continue;

            int index = py * canvasSize + px;
            std::uint32_t colour = pixels[std::size_t(y) * width + x] & 0xFFFFFF;
            bool settled = colour == previous[index];
            previous[index] = colour;
            if (baseline) {
                committed[index] = colour;
                continue;
            }
            if (!settled || colour == committed[index]) continue;
            committed[index] = colour;

            bool echo;
            {
                std::lock_guard guard(brushLock);
                echo = now < echoUntil[index];
            }
            if (!echo) applyPixel(px, py, colour);
        }
    }
    sampledFrames++;
}

void Canvas::applyPixel(int px, int py, std::uint32_t colour) {
    int x = px + canvasMinBlock;
    int z = py + canvasMinBlock;
    auto now = std::chrono::system_clock::now();
    BlockState block = isBlank(colour) ? state(2, 0) : palette.nearest(colour);
    world.set({x, surfaceY, z}, block, "Paint", now);
    for (int y = surfaceY + 1; y < worldHeight; y++)
        if (world.get({x, y, z})) world.set({x, y, z}, 0, "Paint", now);
}

}
