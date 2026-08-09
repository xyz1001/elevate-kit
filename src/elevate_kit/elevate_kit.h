#pragma once

#include "elevate_kit/export.h"

#include <functional>
#include <string>

namespace elevate_kit {

class ELEVATE_KIT_API ElevateKit {
public:
    static void setApplicationId(const std::string &application_id);
    static bool process(int argc, char *argv[]);
    static bool runTask(const std::string &task_name,
                        const std::string &params_json,
                        const std::string &module_id = "");
    static bool isPasswordlessReady();
};

namespace detail {
using TaskHandler = std::function<bool(const std::string &)>;
ELEVATE_KIT_API void registerTask(const std::string &task_name,
                                  TaskHandler handler);

template <typename Handler>
void registerTask(const std::string &task_name, Handler handler) {
    registerTask(task_name, TaskHandler(handler));
}

class TaskRegistrar {
public:
    template <typename Handler>
    TaskRegistrar(const char *task_name, Handler handler) {
        registerTask(std::string(task_name), handler);
    }
};
}  // namespace detail
}  // namespace elevate_kit

#define ELEVATE_KIT_DETAIL_JOIN_IMPL(left, right) left##right
#define ELEVATE_KIT_DETAIL_JOIN(left, right) \
    ELEVATE_KIT_DETAIL_JOIN_IMPL(left, right)
#ifdef __COUNTER__
#define ELEVATE_KIT_DETAIL_UNIQUE \
    ELEVATE_KIT_DETAIL_JOIN(elevate_kit_task_registrar_, __COUNTER__)
#else
#define ELEVATE_KIT_DETAIL_UNIQUE \
    ELEVATE_KIT_DETAIL_JOIN(elevate_kit_task_registrar_, __LINE__)
#endif
#define REGISTER_ELEVATED_TASK(task_name, ...)                         \
    [[maybe_unused]] static const ::elevate_kit::detail::TaskRegistrar \
    ELEVATE_KIT_DETAIL_UNIQUE(task_name, __VA_ARGS__)
