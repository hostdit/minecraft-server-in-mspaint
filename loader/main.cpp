#include <windows.h>
#include <tlhelp32.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

DWORD findProcess(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof entry};
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry)) {
        if (_wcsicmp(entry.szExeFile, name) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring dllPath;

    if (argc > 1) {
        dllPath = argv[1];
    } else {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        auto dir = std::filesystem::path(self).parent_path();
        std::filesystem::path newest;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == L".dll" && entry.path().filename().wstring().starts_with(L"McPaint") && entry.path() > newest)
                newest = entry.path();
        dllPath = newest.empty() ? (dir / L"McPaint.dll").wstring() : newest.wstring();
    }

    if (!std::filesystem::exists(dllPath)) {
        std::wcerr << L"DLL not found: " << dllPath << std::endl;
        return 1;
    }

    dllPath = std::filesystem::absolute(dllPath).wstring();

    DWORD pid = findProcess(L"mspaint.exe");
    if (!pid) {
        std::wcerr << L"mspaint.exe is not running" << std::endl;
        return 1;
    }

    std::wcout << L"Found mspaint.exe pid " << pid << std::endl;

    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        std::wcerr << L"OpenProcess failed: " << GetLastError() << std::endl;
        return 1;
    }

    std::size_t size = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::wcerr << L"VirtualAllocEx failed: " << GetLastError() << std::endl;
        CloseHandle(process);
        return 1;
    }

    if (!WriteProcessMemory(process, remote, dllPath.c_str(), size, nullptr)) {
        std::wcerr << L"WriteProcessMemory failed: " << GetLastError() << std::endl;
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel, "LoadLibraryW"));

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!thread) {
        std::wcerr << L"CreateRemoteThread failed: " << GetLastError() << std::endl;
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    WaitForSingleObject(thread, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);

    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!exitCode) {
        std::wcerr << L"LoadLibrary returned null, DLL failed to load" << std::endl;
        return 1;
    }

    std::wcout << L"McPaint.dll injected into mspaint.exe pid " << pid << std::endl;
    return 0;
}
