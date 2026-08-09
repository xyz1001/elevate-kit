#pragma once

#include <cstdint>
#include <string>
#include "elevate_kit/export.h"

namespace elevate_kit {
std::uint64_t GetPid();

std::string GetProcessPath(std::uint64_t process_id);

std::string GetModulePathFromAddress(const void *address);

bool LoadLibrary(const std::string &module_path);
}  // namespace elevate_kit
