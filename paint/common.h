#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

namespace mcx {

extern HMODULE module;

std::string narrow(std::wstring_view text);
std::wstring widen(std::string_view text);
std::filesystem::path dataDir();
void log(const std::string& line);
bool hostIsPaint();

template <class F>
void guarded(const char* where, F&& body) noexcept {
    try {
        body();
    } catch (const std::exception& error) {
        log(std::format("{} threw: {}", where, error.what()));
    } catch (...) {
        log(std::format("{} threw unknown exception", where));
    }
}

}
