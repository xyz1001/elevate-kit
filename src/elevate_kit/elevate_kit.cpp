#include "elevate_kit.h"

#include "logger.h"

#include <optional>

#include <fmt/format.h>
#include <CLI/CLI.hpp>
#include <sole.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef LoadLibrary
#undef LoadLibrary
#endif
#endif

#include "application_id.h"
#include "name_pipe.h"
#include "platform.h"
#include "silent_elevation.h"
#include "task_registry.h"

namespace elevate_kit {

void SetApplicationId(const std::string &application_id) {
    ApplicationId::Set(application_id);
}

void Process(int argc, char *argv[]) {
    if (argc < 2) {
        return;
    }
    CLI::App app{"elevate-kit internal worker"};
    std::string session_id;
    bool install_requested = false;
    app.add_option("--elevate-pipe", session_id)
            ->required()
            ->expected(1)
            ->multi_option_policy(CLI::MultiOptionPolicy::Throw)
            ->check(CLI::Validator(
                    [](std::string &value) {
                        return value.empty() ? "pipe UUID must not be empty"
                                             : std::string{};
                    },
                    "non-empty UUID"));
    app.add_flag("--elevate-install", install_requested)
            ->expected(0, 1)
            ->multi_option_policy(CLI::MultiOptionPolicy::Throw);
    app.allow_extras(false);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &) {
        return;
    }

#ifdef _WIN32
    FreeConsole();
#endif

    std::unique_ptr<ISilentElevation> elevation = ISilentElevation::Create();
    if (!elevation->IsElevated()) {
        LogWarn("worker process is not elevated");
        _exit(-1);
    }
    if (install_requested) {
        elevation->ConfigureSilentElevation(ApplicationId::Get());
    }
    std::unique_ptr<INamedPipe> pipe =
            INamedPipe::Open(ApplicationId::Get(), session_id);
    if (!pipe || GetProcessPath(pipe->GetRemoteProcessId()) !=
                         GetProcessPath(GetPid())) {
        LogError("worker process failed to connect to host");
        _exit(-1);
    }
    std::optional<std::string> payload = pipe->RecvMessage();
    if (!payload) {
        LogError("worker process failed to receive request");
        _exit(-1);
    }
    LogDebug("worker process received request: {}", *payload);
    try {
        nlohmann::json request = nlohmann::json::parse(*payload);
        std::string module_path = request.value("modulePath", std::string{});
        if (!LoadLibrary(module_path)) {
            LogError("worker process failed to load module: {}", module_path);
        }
        auto result = TaskRegistry::RunTask(
                request.at("taskName").get<std::string>(),
                request.at("params").get<nlohmann::json>());
        LogInfo("work process execute task result: {}", result.dump());
        pipe->SendMessage(result.dump());
    } catch (const nlohmann::json::exception &) {
        LogError("worker process failed to parse request");
        _exit(-1);
    }
    _exit(0);
}

void RegisterTask(const std::string &task_name, Task task) {
    TaskRegistry::RegisterTask(task_name, std::move(task));
}

nlohmann::json CallTask(const std::string &task_name,
                        const nlohmann::json &params,
                        const std::string &module_path) {
    std::string session_id = sole::uuid4().str();
    const std::string application_id = ApplicationId::Get();
    std::unique_ptr<INamedPipe> pipe =
            INamedPipe::Create(application_id, session_id);
    if (!pipe) {
        return {};
    }
    auto elevation = ISilentElevation::Create();
    std::vector<std::string> arguments{"--elevate-pipe=" + session_id};
    if (!elevation->IsSilentElevationConfigured(application_id)) {
        arguments.emplace_back("--elevate-install");
    }
    auto pid = elevation->LaunchElevatedService(arguments);
    if (pid == std::nullopt) {
        return {};
    }
    if (!pipe->Accept(std::chrono::seconds(3))) {
        return {};
    }
    if (pid != pipe->GetRemoteProcessId() &&
        GetProcessPath(GetPid()) !=
                GetProcessPath(pipe->GetRemoteProcessId())) {
        return {};
    }

    nlohmann::json request{{"taskName", task_name},
                           {"params", params},
                           {"modulePath", module_path},
                           {"sessionId", session_id}};
    if (!pipe->SendMessage(request.dump())) {
        return {};
    }
    LogDebug("CallTask request: {}", request.dump());
    std::optional<std::string> payload = pipe->RecvMessage();
    if (!payload) {
        LogError("CallTask failed to receive response");
        return {};
    }
    LogDebug("CallTask response: {}", *payload);
    try {
        auto response = nlohmann::json::parse(*payload);
        if (!response.is_object()) {
            return nlohmann::json();
        }
        return response;
    } catch (const nlohmann::json::exception &) {
        return nlohmann::json();
    }
}

nlohmann::json CallTask(const std::string &task_name,
                        const nlohmann::json &params,
                        const void *module_address) {
    auto module_path = GetModulePathFromAddress(module_address);
    return CallTask(task_name, params, module_path);
}

void SetLogCallbacks(const LogCallbacks &callbacks) {
    Logger::SetCallbacks(callbacks);
}

void ResetLogCallbacks() {
    Logger::Reset();
}

}  // namespace elevate_kit
