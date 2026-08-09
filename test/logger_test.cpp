#include "elevate_kit/logger.h"
#include "elevate_kit/elevate_kit.h"

#include "doctest/doctest.h"

#include <string>
#include <vector>

namespace {
struct ResetGuard {
    ~ResetGuard() {
        elevate_kit::ResetLogCallbacks();
    }
};

std::vector<std::string> debug_messages;
std::vector<std::string> info_messages;
std::vector<std::string> warn_messages;
std::vector<std::string> error_messages;

void CaptureDebug(std::string_view message) {
    debug_messages.emplace_back(message);
}
void CaptureInfo(std::string_view message) {
    info_messages.emplace_back(message);
}
void CaptureWarn(std::string_view message) {
    warn_messages.emplace_back(message);
}
void CaptureError(std::string_view message) {
    error_messages.emplace_back(message);
}
}  // namespace

TEST_CASE("logger dispatches complete messages by level") {
    ResetGuard guard;
    debug_messages.clear();
    info_messages.clear();
    warn_messages.clear();
    error_messages.clear();
    elevate_kit::SetLogCallbacks(
            {CaptureDebug, CaptureInfo, CaptureWarn, CaptureError});

    elevate_kit::Logger::LogDebug("debug message");
    elevate_kit::Logger::LogInfo("info message");
    elevate_kit::Logger::LogWarn("warn message");
    elevate_kit::Logger::LogError("error message");

    REQUIRE(debug_messages.size() == 1);
    REQUIRE(info_messages.size() == 1);
    REQUIRE(warn_messages.size() == 1);
    REQUIRE(error_messages.size() == 1);
    CHECK(debug_messages.front() == "debug message");
    CHECK(info_messages.front() == "info message");
    CHECK(warn_messages.front() == "warn message");
    CHECK(error_messages.front() == "error message");
}

TEST_CASE("logger accepts partial callbacks and reset removes injection") {
    ResetGuard guard;
    debug_messages.clear();
    elevate_kit::SetLogCallbacks({CaptureDebug, nullptr, nullptr, nullptr});

    elevate_kit::Logger::LogDebug("captured before reset");
    elevate_kit::Logger::LogInfo("default output");
    elevate_kit::Logger::LogWarn("default warning output");
    elevate_kit::Logger::LogError("default error output");
    CHECK(debug_messages == std::vector<std::string>{"captured before reset"});

    elevate_kit::ResetLogCallbacks();
    elevate_kit::Logger::LogDebug("default output after reset");
    elevate_kit::Logger::LogInfo("default info after reset");
    elevate_kit::Logger::LogWarn("default warning after reset");
    elevate_kit::Logger::LogError("default error after reset");
    CHECK(debug_messages == std::vector<std::string>{"captured before reset"});
}
