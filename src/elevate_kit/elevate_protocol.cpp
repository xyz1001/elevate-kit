#include "elevate_protocol.h"

#include <nlohmann/json.hpp>

namespace elevate_kit::detail {
namespace {

using json = nlohmann::json;

bool readString(const json &object, const char *name, std::string &value) {
    if (!object.contains(name) || !object.at(name).is_string()) {
        return false;
    }
    value = object.at(name).get<std::string>();
    return true;
}

}  // namespace

bool serializeRequest(const Request &request, std::string &payload) {
    try {
        payload = json{{"task_name", request.task_name},
                       {"params_json", request.params_json},
                       {"module_id", request.module_id},
                       {"request_id", request.request_id}}
                          .dump();
        return true;
    } catch (const json::exception &) {
        return false;
    }
}

bool deserializeRequest(const std::string &payload, Request &request) {
    try {
        const json object = json::parse(payload);
        if (!object.is_object() ||
            !readString(object, "task_name", request.task_name) ||
            !readString(object, "params_json", request.params_json) ||
            !readString(object, "module_id", request.module_id) ||
            !readString(object, "request_id", request.request_id)) {
            return false;
        }
        return true;
    } catch (const json::exception &) {
        return false;
    }
}

bool serializeResponse(bool success, std::string &payload) {
    try {
        payload = json{{"success", success}}.dump();
        return true;
    } catch (const json::exception &) {
        return false;
    }
}

bool deserializeResponse(const std::string &payload, bool &success) {
    try {
        const json object = json::parse(payload);
        if (!object.is_object() || !object.contains("success") ||
            !object.at("success").is_boolean()) {
            return false;
        }
        success = object.at("success").get<bool>();
        return true;
    } catch (const json::exception &) {
        return false;
    }
}

std::array<std::uint8_t, 4> encodePayloadLength(std::uint32_t length) {
    return {static_cast<std::uint8_t>(length >> 24),
            static_cast<std::uint8_t>(length >> 16),
            static_cast<std::uint8_t>(length >> 8),
            static_cast<std::uint8_t>(length)};
}

std::uint32_t decodePayloadLength(const std::array<std::uint8_t, 4> &header) {
    return (static_cast<std::uint32_t>(header[0]) << 24) |
           (static_cast<std::uint32_t>(header[1]) << 16) |
           (static_cast<std::uint32_t>(header[2]) << 8) |
           static_cast<std::uint32_t>(header[3]);
}

}  // namespace elevate_kit::detail
