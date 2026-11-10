#pragma once

#include "canvas.h"

#include <mcx/autosave.h>
#include <mcx/server.h>

#include <atomic>
#include <memory>

namespace mcx {

class Host {
public:
    static Host& get();

    void start();
    void stop();

private:
    Host();

    std::atomic<bool> running = false;
    std::unique_ptr<World> worldState;
    std::unique_ptr<Autosave> autosave;
    std::unique_ptr<Server> serverState;
    std::unique_ptr<Canvas> canvas;

    void run();
};

}
