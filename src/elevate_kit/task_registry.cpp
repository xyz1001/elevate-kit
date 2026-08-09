#include "task_registry.h"

#include <unordered_map>
#include <utility>

namespace elevate_kit::detail {
namespace {
std::unordered_map<std::string, TaskHandler> &tasks() {
    static std::unordered_map<std::string, TaskHandler> instance;
    return instance;
}
}  // namespace

void registerTask(const std::string &task_name, TaskHandler handler) {
    if (task_name.empty() || !handler) {
        return;
    }
    tasks()[task_name] = std::move(handler);
}

bool runTask(const std::string &task_name, const std::string &params_json) {
    const auto it = tasks().find(task_name);
    if (it == tasks().end()) {
        return false;
    }
    return it->second(params_json);
}
}  // namespace elevate_kit::detail
