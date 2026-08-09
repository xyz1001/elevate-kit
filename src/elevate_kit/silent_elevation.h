#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elevate_kit {
class ISilentElevation {
public:
    virtual ~ISilentElevation() = default;
    static std::unique_ptr<ISilentElevation> Create();

public:
    virtual bool IsElevated() = 0;
    virtual bool IsSilentElevationConfigured(
            const std::string &application_id) = 0;
    virtual bool ConfigureSilentElevation(
            const std::string &application_id) = 0;
    virtual std::optional<std::uint64_t> LaunchElevatedService(
            const std::vector<std::string> &arguments) = 0;
};
}  // namespace elevate_kit
