#pragma once

#include "elevate_kit/export.h"

#include <functional>
#include <string>

namespace elevate_kit::detail {
using TaskHandler = std::function<bool(const std::string &)>;
ELEVATE_KIT_API void registerTask(const std::string &task_name,
                                  TaskHandler handler);
bool runTask(const std::string &task_name, const std::string &params_json);
}  // namespace elevate_kit::detail
