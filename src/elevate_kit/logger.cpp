#include "logger.h"

#include <cstdio>

#include <fmt/format.h>

namespace elevate_kit {
namespace {

void DefaultDebug(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stdout);
    fmt::println(stdout, "");
}

void DefaultInfo(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stdout);
    fmt::println(stdout, "");
}

void DefaultWarn(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    fmt::println(stderr, "");
}

void DefaultError(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    fmt::println(stderr, "");
}

}  // namespace

Logger &Logger::Instance() {
    static Logger logger;
    return logger;
}

Logger::Logger()
        : logd_(DefaultDebug),
          logi_(DefaultInfo),
          logw_(DefaultWarn),
          loge_(DefaultError) {}

void Logger::SetCallbacks(const LogCallbacks &callbacks) {
    Instance().logd_.store(
            callbacks.logd != nullptr ? callbacks.logd : DefaultDebug,
            std::memory_order_release);
    Instance().logi_.store(
            callbacks.logi != nullptr ? callbacks.logi : DefaultInfo,
            std::memory_order_release);
    Instance().logw_.store(
            callbacks.logw != nullptr ? callbacks.logw : DefaultWarn,
            std::memory_order_release);
    Instance().loge_.store(
            callbacks.loge != nullptr ? callbacks.loge : DefaultError,
            std::memory_order_release);
}

void Logger::Reset() {
    SetCallbacks({});
}

void Logger::LogDebug(std::string_view message) {
    Instance().logd_.load(std::memory_order_acquire)(message);
}
void Logger::LogInfo(std::string_view message) {
    Instance().logi_.load(std::memory_order_acquire)(message);
}
void Logger::LogWarn(std::string_view message) {
    Instance().logw_.load(std::memory_order_acquire)(message);
}
void Logger::LogError(std::string_view message) {
    Instance().loge_.load(std::memory_order_acquire)(message);
}

}  // namespace elevate_kit
