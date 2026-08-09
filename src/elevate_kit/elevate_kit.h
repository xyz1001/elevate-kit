/*
 * author: xyz1001 <zgzf1001@gmail.com>
 * created at: 2026-08-12 11:22:56
 * coding: utf-8
 * license: MIT
 */

#pragma once

#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "elevate_kit/export.h"

namespace elevate_kit {

/**
 * @brief Sets the application identifier used for elevation and IPC.
 * @param application_id Non-empty identifier containing only ASCII letters,
 *        digits, '.', '_', or '-'.
 * @throws std::invalid_argument if @p application_id is empty or contains
 *         another character. The previous identifier is retained on failure.
 * @note The identifier may be changed repeatedly.
 */
ELEVATE_KIT_API void SetApplicationId(const std::string &application_id);

/**
 * @brief Processes an elevate-kit internal-worker invocation.
 * @param argc Argument count, as passed to the process entry point.
 * @param argv Argument vector, as passed to the process entry point.
 * @warning This function is for the internal worker path, not normal
 *          application argument processing. A non-worker invocation, including
 *          one with fewer than two arguments or invalid worker options, returns
 *          without taking further action.
 * @note A valid worker request runs in the elevated worker and exits the
 *       process with status 0 after processing the request. Failure to
 *       elevate, connect, receive, or parse the request exits the process
 *       with status -1.
 */
ELEVATE_KIT_API void Process(int argc, char *argv[]);

/**
 * @brief A callback that receives a task's JSON parameters.
 * @param params Parameters supplied to the task.
 * @return The JSON result sent back to the caller.
 * @note The callback is invoked synchronously when the registered task is
 *       run. Its result may be any JSON value.
 */
using Task = std::function<nlohmann::json(const nlohmann::json &)>;

/**
 * @brief Registers or replaces a named task.
 * @param task_name Name used to call the task.
 * @param task Callback to invoke with the call parameters.
 * @note An empty name or an empty callback is ignored. A valid callback
 *       replaces the task previously registered under the same name.
 */
ELEVATE_KIT_API void RegisterTask(const std::string &task_name, Task task);

/**
 * @brief Calls a task in an elevated worker process.
 * @param task_name Registered task name.
 * @param params JSON value passed unchanged to the task callback.
 * @param module_path Path to the module containing the task registration;
 *        the worker attempts to load this path before running the task.
 * @return The task's JSON result, or JSON null if setup, communication,
 *         response parsing, or response validation fails.
 * @note This creates an IPC session, may configure silent elevation, launches
 *       an elevated worker, and waits up to three seconds for its connection.
 *       The worker also verifies the connected process identity.
 */
ELEVATE_KIT_API nlohmann::json CallTask(const std::string &task_name,
                                        const nlohmann::json &params,
                                        const std::string &module_path);

/**
 * @brief Calls a task in an elevated worker using a module address.
 * @param task_name Registered task name.
 * @param params JSON value passed unchanged to the task callback.
 * @param module_address Address belonging to the module containing the task;
 *        its loaded module path is resolved and used for the worker call.
 * @return The task's JSON result, or JSON null if the path cannot be resolved
 *         or the call otherwise fails.
 * @note This overload has the same IPC, elevation, loading, and timeout side
 *       effects as the module-path overload.
 */
ELEVATE_KIT_API nlohmann::json CallTask(const std::string &task_name,
                                        const nlohmann::json &params,
                                        const void *module_address);

/**
 * @brief Calls a task using the module containing this inline call site.
 * @param task_name Registered task name.
 * @param params JSON value passed unchanged to the task callback.
 * @return The task's JSON result, or JSON null if the call fails.
 * @note The no-inline marker preserves an address from which the module path
 *       is resolved; this overload otherwise has the same side effects and
 *       failure behavior as the other CallTask overloads.
 */
static ELEVATE_KIT_NOINLINE nlohmann::json CallTask(
        const std::string &task_name, const nlohmann::json &params) {
    using Function =
            nlohmann::json (*)(const std::string &, const nlohmann::json &);
    const auto address = static_cast<Function>(&CallTask);
    return CallTask(task_name, params, reinterpret_cast<const void *>(address));
}

/**
 * @brief Receives one formatted log message.
 * @param message Message view, valid only during the callback invocation.
 */
using LogCallback = void (*)(std::string_view message);

/**
 * @brief Logging callbacks, one for each severity.
 * @note A null member selects that severity's built-in callback. Callback
 *       pointers are copied and are not retained as callable objects; the
 *       message view is valid only for the callback invocation.
 */
struct LogCallbacks {
    /** @brief Receives debug messages. */
    LogCallback logd = nullptr;
    /** @brief Receives informational messages. */
    LogCallback logi = nullptr;
    /** @brief Receives warning messages. */
    LogCallback logw = nullptr;
    /** @brief Receives error messages. */
    LogCallback loge = nullptr;
};

/**
 * @brief Installs logging callbacks.
 * @param callbacks Callback pointers to use by severity.
 * @note Null members retain the corresponding default output callback. The
 *       defaults write debug and informational messages to stdout, and
 *       warnings and errors to stderr.
 * @warning Each non-null callback must remain valid until it is replaced or
 *          the callbacks are reset.
 */
ELEVATE_KIT_API void SetLogCallbacks(const LogCallbacks &callbacks);

/**
 * @brief Restores all built-in logging callbacks.
 * @note Debug and informational messages go to stdout; warnings and errors go
 *       to stderr.
 */
ELEVATE_KIT_API void ResetLogCallbacks();

}  // namespace elevate_kit

/**
 * @brief Registers a task during static initialization.
 * @param task_name Identifier used both as the C++ static variable name and
 *        as the registered task name.
 * @param ... Task callback expression passed to RegisterTask.
 * @warning @p task_name must be a valid identifier and must not conflict with
 *          another declaration in the same scope. The callback must be valid
 *          at static initialization time; registration is ignored for an
 *          empty name or empty callback.
 */
#define REGISTER_ELEVATED_TASK(task_name, ...)                \
    [[maybe_unused]] static const bool task_name = [] {       \
        ::elevate_kit::RegisterTask(#task_name, __VA_ARGS__); \
        return true;                                          \
    }()
