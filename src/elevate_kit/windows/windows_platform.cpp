#include "../platform.h"

#include "../logger.h"

#include <cstdint>

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef SendMessage
#undef SendMessage
#endif
#ifdef LoadLibrary
#undef LoadLibrary
#endif

#include <fmt/format.h>

#include "common.h"

namespace elevate_kit {

uint64_t GetPid() {
    return static_cast<uint64_t>(GetCurrentProcessId());
}

std::string GetProcessPath(uint64_t process_id) {
    HANDLE process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(process_id)));
    if (process == nullptr) {
        return {};
    }
    constexpr auto kMaxPath = 32768;
    std::wstring path(kMaxPath, L'\0');
    auto size = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &size) == FALSE) {
        return {};
    }
    path.resize(size);
    return WideToUtf8(path);
}

std::string GetModulePathFromAddress(const void *address) {
    if (address == nullptr) {
        return {};
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(address),
                           &module) == FALSE) {
        return {};
    }

    std::wstring result;
    DWORD size = MAX_PATH;
    while (true) {
        std::vector<wchar_t> buffer(size, L'\0');
        DWORD copied = GetModuleFileNameW(module, buffer.data(), size);
        if (copied == 0) {
            break;
        }
        if (copied < size - 1) {
            std::wstring path(buffer.data(), copied);
            result = std::move(path);
            break;
        }
        size *= 2;
    }
    FreeLibrary(module);

    return WideToUtf8(result);
}

bool LoadLibrary(const std::string &module_path) {
    if (module_path.empty() || module_path == GetProcessPath(GetPid())) {
        return true;
    }
    std::wstring wide(module_path.begin(), module_path.end());
    HMODULE handle = LoadLibraryW(wide.c_str());
    if (handle == nullptr) {
        LogError("failed to load module '{}': error={}", module_path,
                         GetLastError());
        return false;
    }
    return true;
}

}  // namespace elevate_kit
