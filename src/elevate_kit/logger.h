#pragma once

#include "elevate_kit/elevate_kit.h"

#include <atomic>
#include <string_view>
#include <utility>

#include <fmt/format.h>

namespace elevate_kit {

template <typename... Args>
void LogDebug(fmt::format_string<Args...> format, Args &&...args);

template <typename... Args>
void LogInfo(fmt::format_string<Args...> format, Args &&...args);

template <typename... Args>
void LogWarn(fmt::format_string<Args...> format, Args &&...args);

template <typename... Args>
void LogError(fmt::format_string<Args...> format, Args &&...args);

class Logger {
public:
    static void SetCallbacks(const LogCallbacks &callbacks);
    static void Reset();

    static void LogDebug(std::string_view message);
    static void LogInfo(std::string_view message);
    static void LogWarn(std::string_view message);
    static void LogError(std::string_view message);

private:
    Logger();
    static Logger &Instance();

private:
    std::atomic<LogCallback> logd_;
    std::atomic<LogCallback> logi_;
    std::atomic<LogCallback> logw_;
    std::atomic<LogCallback> loge_;
};

template <typename... Args>
void LogDebug(fmt::format_string<Args...> format, Args &&...args) {
    Logger::LogDebug(fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void LogInfo(fmt::format_string<Args...> format, Args &&...args) {
    Logger::LogInfo(fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void LogWarn(fmt::format_string<Args...> format, Args &&...args) {
    Logger::LogWarn(fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void LogError(fmt::format_string<Args...> format, Args &&...args) {
    Logger::LogError(fmt::format(format, std::forward<Args>(args)...));
}

}  // namespace elevate_kit
