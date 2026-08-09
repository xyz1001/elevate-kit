#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../platform.h"
#include "../silent_elevation.h"
#include "../logger.h"
#include "templates.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>

namespace elevate_kit {
namespace {
std::optional<std::uint64_t> Execute(const std::vector<std::string> &args,
                                     bool wait) {
    std::vector<char *> av;
    for (const auto &a : args) {
        av.push_back(const_cast<char *>(a.c_str()));
    }
    av.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        LogError("fork failed: errno={}", errno);
        return std::nullopt;
    }
    if (pid == 0) {
        execvp(av[0], av.data());
        LogError("execv failed: path='{}' errno={}",
                         args.empty() ? std::string() : args[0], errno);
        _exit(127);
    }
    if (!wait) {
        LogDebug("started pid={}", pid);
        return static_cast<std::uint64_t>(pid);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != pid) {
        LogError("waitpid failed: pid={} errno={}", pid, errno);
        return std::nullopt;
    }
    if (WIFSIGNALED(status)) {
        LogWarn("child terminated by signal: pid={} signal={}", pid,
                        WTERMSIG(status));
        return std::nullopt;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LogWarn("child exited nonzero: pid={} exit={}", pid,
                        WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(pid);
}

bool WriteFile(const std::string &path, const std::string &contents) {
    std::ofstream fout(path);
    if (!fout) {
        LogError("file open failed: path='{}' errno={}", path, errno);
        return false;
    }
    fout.write(contents.data(), contents.size());
    if (!fout) {
        LogError("file write failed: path='{}' errno={}", path, errno);
        return false;
    }
    return true;
}

std::string ReadFile(const std::string &path) {
    std::ifstream fin(path);
    if (!fin) {
        LogError("file open failed: path='{}' errno={}", path, errno);
        return {};
    }
    std::string contents((std::istreambuf_iterator<char>(fin)), {});
    if (!fin) {
        LogError("file read failed: path='{}' errno={}", path, errno);
        return {};
    }
    return contents;
}

}  // namespace

class LinuxSilentElevation final : public ISilentElevation {
public:
    LinuxSilentElevation() = default;
    ~LinuxSilentElevation() override = default;

public:
    bool IsElevated() override {
        return geteuid() == 0;
    }

    bool IsSilentElevationConfigured(
            const std::string &application_id) override {
        std::string path = GetProcessPath(GetPid());
        std::string action_name = GeneratePolkitActionName(application_id);
        LogDebug("checking silent elevation: host='{}' action='{}'", path,
                         action_name);
        if (!CheckExecutableFile(path)) {
            return false;
        }
        if (ReadFile(GetPolkitActionPath(action_name)) !=
            fmt::format(kLinuxPolkitPolicyTemplate, action_name, path)) {
            LogWarn("silent elevation policy file exists and matches");
        }
        if (ReadFile(GetPolkitRulePath(action_name)) !=
            fmt::format(kLinuxPolkitRuleTemplate, action_name)) {
            LogWarn("silent elevation rule file exists and matches");
        }
        std::vector<std::string> args{"pkcheck", "--action-id", action_name,
                                      "--process", std::to_string(getpid())};
        return Execute(args, true).has_value();
    }

    bool ConfigureSilentElevation(const std::string &application_id) override {
        LogInfo("ConfigureSilentElevation entered");
        std::string path = GetProcessPath(GetPid());
        if (!CheckExecutableFile(path)) {
            LogError("Path validation failed: path='{}'", path);
            return false;
        }

        std::string action_name = GeneratePolkitActionName(application_id);

        auto policy_path = GetPolkitActionPath(action_name);
        std::string policy =
                fmt::format(kLinuxPolkitPolicyTemplate, action_name, path);
        if (!WriteFile(policy_path, policy)) {
            LogError("Failed to write policy file");
            return false;
        }
        if (chmod(policy_path.c_str(), 0644) != 0) {
            LogError("policy chmod failed: errno={}", errno);
        }

        auto rule_path = GetPolkitRulePath(action_name);
        std::string rule = fmt::format(kLinuxPolkitRuleTemplate, action_name);
        if (!WriteFile(rule_path, rule)) {
            LogError("Failed to write rule file");
            return false;
        }
        if (chmod(rule_path.c_str(), 0644) != 0) {
            LogError("rule chmod failed: errno={}", errno);
        }
        return true;
    }

    std::optional<std::uint64_t> LaunchElevatedService(
            const std::vector<std::string> &arguments) override {
        std::string path = GetProcessPath(GetPid());
        std::vector<std::string> args;
        args.reserve(arguments.size() + 2);
        args.push_back("pkexec");
        args.push_back(path);
        args.insert(args.end(), arguments.begin(), arguments.end());
        return Execute(args, false);
    }

private:
    static bool CheckExecutableFile(const std::string &path) {
        if (path.empty()) {
            LogError("target path check failed: path is empty");
            return false;
        }
        LogDebug("target path check: path='{}'", path);
        struct stat information{};
        if (stat(path.c_str(), &information) != 0) {
            LogError("target stat failed: errno={}", errno);
            return false;
        }
        if ((information.st_mode & S_IWOTH) != 0) {
            LogWarn("target path rejected as world-writable: mode={:#o}",
                            static_cast<unsigned int>(information.st_mode & 07777));
            return false;
        }
#ifdef NDEBUG
        if (information.st_uid != 0) {
            LogWarn("target path rejected for non-root owner: uid={}",
                            information.st_uid);
            return false;
        }
#endif
        return true;
    }

    static std::string GeneratePolkitActionName(
            const std::string &application_id) {
        return fmt::format("com.{}.elevatekit", application_id);
    }

    static std::string GetPolkitActionPath(const std::string &action_name) {
        return fmt::format("/usr/share/polkit-1/actions/{}.policy",
                           action_name);
    }

    static std::string GetPolkitRulePath(const std::string &action_name) {
        return fmt::format("/etc/polkit-1/rules.d/49-{}.rules", action_name);
    }
};

std::unique_ptr<ISilentElevation> ISilentElevation::Create() {
    return std::make_unique<LinuxSilentElevation>();
}

}  // namespace elevate_kit
