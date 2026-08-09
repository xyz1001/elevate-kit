#include "task_registry.h"

#include <unordered_map>
#include <utility>

namespace elevate_kit {

TaskRegistry::TaskRegistry() = default;

TaskRegistry::~TaskRegistry() = default;

TaskRegistry &TaskRegistry::Instance() {
    static TaskRegistry instance;
    return instance;
}

void TaskRegistry::RegisterTask(const std::string &task_name, Task task) {
    if (task_name.empty() || !task) {
        return;
    }
    Instance().tasks_[task_name] = std::move(task);
}

nlohmann::json TaskRegistry::RunTask(const std::string &task_name,
                                     const nlohmann::json &params) {
    auto it = Instance().tasks_.find(task_name);
    if (it == Instance().tasks_.end()) {
        return false;
    }
    return it->second(params);
}

}  // namespace elevate_kit
