#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../platform.h"

#include "../logger.h"

#include <climits>

#include <array>
#include <string>

#include <dlfcn.h>
#include <unistd.h>

#include <fmt/format.h>

namespace elevate_kit {

std::uint64_t GetPid() {
    return static_cast<std::uint64_t>(getpid());
}

std::string GetProcessPath(std::uint64_t process_id) {
    std::array<char, 4096> buffer{};
    std::string proc_path = "/proc/" + std::to_string(process_id) + "/exe";
    ssize_t size = readlink(proc_path.c_str(), buffer.data(), buffer.size());
    return size > 0 ? std::string(buffer.data(), static_cast<size_t>(size))
                    : std::string();
}

std::string GetModulePathFromAddress(const void *address) {
    std::string module_path;
    Dl_info info{};
    if (address != nullptr && dladdr(address, &info) != 0 &&
        info.dli_fname != nullptr) {
        module_path = info.dli_fname;
        std::array<char, PATH_MAX> resolved{};
        if (realpath(module_path.c_str(), resolved.data()) != nullptr) {
            module_path = resolved.data();
        }
    }
    if (module_path.empty()) {
        module_path = GetProcessPath(GetPid());
    }
    return module_path;
}

bool LoadLibrary(const std::string &module_path) {
    if (module_path.empty() || module_path == GetProcessPath(GetPid())) {
        return true;
    }
    void *handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        const char *error = dlerror();
        LogError("failed to load module {}: {}", module_path,
                         error == nullptr ? "unknown error" : error);
        return false;
    }
    return true;
}

}  // namespace elevate_kit
