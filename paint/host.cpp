#include "host.h"

#include "common.h"
#include "palette.h"

#include <mcx/net.h>
#include <mcx/png.h>

#include <thread>

namespace mcx {

namespace {

constexpr int serverPort = 25565;
constexpr auto canvasPollInterval = std::chrono::milliseconds(100);

std::string paintFavicon() {
    constexpr int size = 64;
    std::vector<std::uint8_t> rgba(size * size * 4, 0);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int i = (y * size + x) * 4;
            int cx = x - 32, cy = y - 34;
            if (cx * cx * 3 + cy * cy * 5 < 2200) {
                rgba[i] = 245; rgba[i + 1] = 240; rgba[i + 2] = 230; rgba[i + 3] = 255;
            }
        }
    }

    struct Blob { int x, y, r; std::uint8_t red, green, blue; };
    Blob blobs[] = {
        {20, 20, 7, 220, 40, 40},
        {44, 20, 7, 40, 120, 220},
        {14, 36, 6, 250, 210, 20},
        {32, 26, 7, 40, 180, 60},
        {50, 36, 6, 180, 60, 200},
        {32, 46, 6, 240, 140, 40},
    };

    for (auto& bl : blobs) {
        for (int dy = -bl.r; dy <= bl.r; dy++) {
            for (int dx = -bl.r; dx <= bl.r; dx++) {
                if (dx * dx + dy * dy > bl.r * bl.r) continue;
                int px = bl.x + dx, py = bl.y + dy;
                if (px < 0 || px >= size || py < 0 || py >= size) continue;
                int i = (py * size + px) * 4;
                rgba[i] = bl.red; rgba[i + 1] = bl.green; rgba[i + 2] = bl.blue; rgba[i + 3] = 255;
            }
        }
    }

    int handleX = 52, handleY = 52;
    for (int i = 0; i < 12; i++) {
        int hx = handleX + i / 2, hy = handleY + i / 2;
        if (hx < size && hy < size) {
            int idx = (hy * size + hx) * 4;
            rgba[idx] = 139; rgba[idx + 1] = 90; rgba[idx + 2] = 43; rgba[idx + 3] = 255;
        }
    }

    auto png = encodePng(size, size, rgba);
    return "data:image/png;base64," + base64(png);
}

}

Host& Host::get() {
    static Host* host = new Host();
    return *host;
}

Host::Host() {}

void Host::start() {
    if (running.exchange(true)) return;

    guarded("host", [this] {
        run();
    });
}

void Host::stop() {
    if (!running.exchange(false)) return;

    guarded("host stop", [this] {
        if (serverState) serverState->stop("Paint closed");
        canvas.reset();
        serverState.reset();
        autosave.reset();
        worldState.reset();
    });
}

void Host::run() {
    netStartup();

    auto path = dataDir() / L"world.bin";
    worldState = loadWorld(path, std::chrono::system_clock::now(), log);

    ServerOptions options;
    options.port = serverPort;
    options.host = "Microsoft Paint";
    options.favicon = paintFavicon();
    options.motdSuffix = std::format("{}x{} canvas", canvasSize, canvasSize);
    options.log = log;

    serverState = std::make_unique<Server>(*worldState, options);

    if (serverState->start() == StartResult::portInUse) {
        log("port 25565 is already in use");
        running = false;
        return;
    }

    autosave = std::make_unique<Autosave>(*worldState, path, std::chrono::seconds(1), log);

    log(serverState->motd());

    canvas = std::make_unique<Canvas>(*worldState);

    worldState->onChange([this](const Change& change) {
        guarded("canvas write", [&] {
            if (canvas) canvas->blockChanged(change);
        });
    });

    bool paintOpen = true;
    while (running && paintOpen) {
        guarded("canvas tick", [&] {
            paintOpen = canvas->tick();
        });
        std::this_thread::sleep_for(canvasPollInterval);
    }
    stop();
}

}
