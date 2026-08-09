#pragma once

#include "elevate_kit/export.h"

#include <array>
#include <cstdint>
#include <string>

namespace elevate_kit::detail {

struct Request {
    std::string task_name;
    std::string params_json;
    std::string module_id;
    std::string request_id;
};

ELEVATE_KIT_API bool serializeRequest(const Request &request,
                                      std::string &payload);
ELEVATE_KIT_API bool deserializeRequest(const std::string &payload,
                                        Request &request);
ELEVATE_KIT_API bool serializeResponse(bool success, std::string &payload);
ELEVATE_KIT_API bool deserializeResponse(const std::string &payload,
                                         bool &success);

ELEVATE_KIT_API std::array<std::uint8_t, 4> encodePayloadLength(
        std::uint32_t length);
ELEVATE_KIT_API std::uint32_t decodePayloadLength(
        const std::array<std::uint8_t, 4> &header);

}  // namespace elevate_kit::detail
