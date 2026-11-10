#include "common.h"

#include <chrono>

namespace mcx {

HMODULE module;

namespace {
std::mutex logLock;
}

std::string narrow(std::wstring_view text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring widen(std::string_view text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

std::filesystem::path dataDir() {
    PWSTR raw;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
        throw std::runtime_error("no LocalAppData");
    auto dir = std::filesystem::path(raw) / L"McPaint";
    CoTaskMemFree(raw);
    std::filesystem::create_directories(dir);
    return dir;
}

void log(const std::string& line) {
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    auto text = std::format("{:%F %T} pid {} tid {} {}\n", now, GetCurrentProcessId(), GetCurrentThreadId(), line);
    OutputDebugStringW(widen(text).c_str());
    std::lock_guard guard(logLock);
    std::ofstream(dataDir() / L"log.txt", std::ios::app | std::ios::binary) << text;
}

bool hostIsPaint() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return _wcsicmp(PathFindFileNameW(path), L"mspaint.exe") == 0;
}

}
