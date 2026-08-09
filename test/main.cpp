#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "elevate_kit/application_id.h"
#include "elevate_kit/elevate_kit.h"
#include "elevate_kit/elevate_protocol.h"

#include <array>
#include <cstdint>
#include <string>

namespace {
int legacy_invocations = 0;
bool legacy_handler(const std::string &) {
    ++legacy_invocations;
    return true;
}
}  // namespace

REGISTER_ELEVATED_TASK("legacy_task_unique", legacy_handler);

TEST_CASE("application ID is process-wide and empty means fallback") {
    CHECK(elevate_kit::detail::configuredApplicationId().empty());

    elevate_kit::ElevateKit::setApplicationId("host-a");
    CHECK(elevate_kit::detail::configuredApplicationId() == "host-a");

    elevate_kit::ElevateKit::setApplicationId("");
    CHECK(elevate_kit::detail::configuredApplicationId().empty());
}

TEST_CASE("request JSON round trips strings") {
    const elevate_kit::detail::Request input{"任务\"名", "{\"值\": \"空\"}",
                                             "模块\\id", "请求-1"};
    elevate_kit::detail::Request output;
    std::string payload;

    CHECK(elevate_kit::detail::serializeRequest(input, payload));
    CHECK(elevate_kit::detail::deserializeRequest(payload, output));
    CHECK(output.task_name == input.task_name);
    CHECK(output.params_json == input.params_json);
    CHECK(output.module_id == input.module_id);
    CHECK(output.request_id == input.request_id);
}

TEST_CASE("request JSON accepts empty strings and ignores extra fields") {
    const elevate_kit::detail::Request input{"", "", "", ""};
    elevate_kit::detail::Request output;
    std::string payload;
    CHECK(elevate_kit::detail::serializeRequest(input, payload));
    CHECK(elevate_kit::detail::deserializeRequest(
            R"({"task_name":"","params_json":"","module_id":"","request_id":"","extra":true})",
            output));
    CHECK(output.task_name.empty());
    CHECK(output.params_json.empty());
    CHECK(output.module_id.empty());
    CHECK(output.request_id.empty());
}

TEST_CASE("request JSON rejects missing and incorrectly typed fields") {
    elevate_kit::detail::Request request;
    CHECK_FALSE(elevate_kit::detail::deserializeRequest(
            R"({"task_name":"task","params_json":"{}","module_id":""})",
            request));
    CHECK_FALSE(elevate_kit::detail::deserializeRequest(
            R"({"task_name":false,"params_json":"{}","module_id":"","request_id":""})",
            request));
    CHECK_FALSE(elevate_kit::detail::deserializeRequest(
            R"({"task_name":"task","params_json":[],"module_id":"","request_id":""})",
            request));
}

TEST_CASE("request JSON rejects invalid UTF-8") {
    const elevate_kit::detail::Request request{"\xff", "", "", ""};
    std::string payload;
    CHECK_FALSE(elevate_kit::detail::serializeRequest(request, payload));
}

TEST_CASE("response JSON round trips the success bool") {
    std::string payload;
    bool success = false;
    CHECK(elevate_kit::detail::serializeResponse(true, payload));
    CHECK(payload == R"({"success":true})");
    CHECK(elevate_kit::detail::deserializeResponse(
            R"({"success":false,"extra":"ignored"})", success));
    CHECK_FALSE(success);
}

TEST_CASE("response JSON rejects missing and incorrectly typed success") {
    bool success = false;
    CHECK_FALSE(elevate_kit::detail::deserializeResponse(R"({})", success));
    CHECK_FALSE(elevate_kit::detail::deserializeResponse(
            R"({"success":"true"})", success));
}

TEST_CASE("JSON framing length is 32-bit big-endian") {
    const std::uint32_t length = 0x12345678;
    const std::array<std::uint8_t, 4> header =
            elevate_kit::detail::encodePayloadLength(length);
    CHECK(header == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0x78});
    CHECK(elevate_kit::detail::decodePayloadLength(header) == length);
}

TEST_CASE("legacy elevated worker CLI is consumed without dispatch") {
    char program[] = "program";
    char elevated[] = "--elevated";
    char task[] = "legacy_task_unique";
    char params[] = "{}";
    char *argv[] = {program, elevated, task, params};
    CHECK(elevate_kit::ElevateKit::process(4, argv));
    CHECK(legacy_invocations == 0);
}

TEST_CASE("non-internal CLI is not consumed") {
    char program[] = "program";
    char ordinary[] = "--ordinary";
    char *argv[] = {program, ordinary};
    CHECK_FALSE(elevate_kit::ElevateKit::process(2, argv));
}
