#pragma once

#include "elevate_kit/export.h"

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace elevate_kit {
class TaskRegistry {
public:
    ~TaskRegistry();

public:
    using Task = std::function<nlohmann::json(const nlohmann::json &)>;

    static void RegisterTask(const std::string &task_name, Task task);

    static nlohmann::json RunTask(const std::string &task_name,
                                  const nlohmann::json &params);

private:
    TaskRegistry();

    static TaskRegistry &Instance();

private:
    std::unordered_map<std::string, Task> tasks_;
};
}  // namespace elevate_kit
