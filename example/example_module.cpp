#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#include "example_module.h"

#include "elevate_kit/elevate_kit.h"

REGISTER_ELEVATED_TASK(example_module_task, [](const nlohmann::json &params) {
    std::error_code error;
    const auto size = std::filesystem::file_size(
            params.at("path").get<std::string>(), error);
    if (error) {
        return nlohmann::json{{"filesize", 0}};
    }
    return nlohmann::json{{"filesize", size}};
});

size_t CalcFileSize(const std::string &path) {
    const auto result = elevate_kit::CallTask(
            "example_module_task", nlohmann::json{{"path", path}},
            reinterpret_cast<const void *>(&CalcFileSize));
    if (!result.is_object() || !result.contains("filesize")) {
        return 0;
    }

    const auto &value = result.at("filesize");
    if (!value.is_number_unsigned()) {
        return 0;
    }
    const auto size = value.get<std::uintmax_t>();
    if (size > (std::numeric_limits<size_t>::max)()) {
        return 0;
    }
    return static_cast<size_t>(size);
}
