#include "host.h"
#include "common.h"

namespace {

DWORD WINAPI initThread(void*) {
    if (!mcx::hostIsPaint()) return 0;
    mcx::guarded("init", [] {
        mcx::log("McPaint loaded");
        mcx::Host::get().start();
    });
    return 0;
}

}

extern "C" __declspec(dllexport) void McPaintStart() {
    mcx::Host::get().start();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        mcx::module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
