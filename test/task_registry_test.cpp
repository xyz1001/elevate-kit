#include "elevate_kit/task_registry.h"

#include "doctest/doctest.h"

namespace {
constexpr const char *kPrefix = "task_registry_test__";
}

TEST_CASE("TaskRegistry registers and runs a task with JSON parameters") {
    const std::string task_name = std::string(kPrefix) + "register_and_run";
    const nlohmann::json expected_params = {
            {"name", "elevate-kit"}, {"count", 3}, {"enabled", true}};
    bool received_expected_params = false;

    elevate_kit::TaskRegistry::RegisterTask(
            task_name, [&received_expected_params](const nlohmann::json &params) {
                received_expected_params =
                        params == nlohmann::json{{"name", "elevate-kit"},
                                                 {"count", 3},
                                                 {"enabled", true}};
                return nlohmann::json{{"status", "ok"}, {"value", 42}};
            });

    CHECK(elevate_kit::TaskRegistry::RunTask(task_name, expected_params) ==
          nlohmann::json{{"status", "ok"}, {"value", 42}});
    CHECK(received_expected_params);
}

TEST_CASE("TaskRegistry returns false for an unknown task") {
    const std::string task_name = std::string(kPrefix) + "unknown";

    const auto result = elevate_kit::TaskRegistry::RunTask(
            task_name, nlohmann::json::object());

    CHECK(result.is_boolean());
    CHECK_FALSE(result.get<bool>());
}

TEST_CASE("TaskRegistry ignores empty task names and callbacks") {
    const std::string empty_callback_name =
            std::string(kPrefix) + "empty_callback";

    elevate_kit::TaskRegistry::RegisterTask(
            "", [](const nlohmann::json &) { return nlohmann::json(true); });
    elevate_kit::TaskRegistry::RegisterTask(empty_callback_name,
                                            elevate_kit::TaskRegistry::Task{});

    CHECK_FALSE(elevate_kit::TaskRegistry::RunTask(
                        "", nlohmann::json::object())
                        .get<bool>());
    CHECK_FALSE(elevate_kit::TaskRegistry::RunTask(
                        empty_callback_name, nlohmann::json::object())
                        .get<bool>());
}

TEST_CASE("TaskRegistry replaces a task registered under the same name") {
    const std::string task_name = std::string(kPrefix) + "duplicate";

    elevate_kit::TaskRegistry::RegisterTask(
            task_name, [](const nlohmann::json &) { return nlohmann::json(1); });
    elevate_kit::TaskRegistry::RegisterTask(
            task_name, [](const nlohmann::json &) { return nlohmann::json(2); });

    CHECK(elevate_kit::TaskRegistry::RunTask(task_name,
                                             nlohmann::json::object()) == 2);
}

TEST_CASE("TaskRegistry passes object array and scalar JSON parameters") {
    const std::string object_name = std::string(kPrefix) + "object";
    const std::string array_name = std::string(kPrefix) + "array";
    const std::string scalar_name = std::string(kPrefix) + "scalar";

    elevate_kit::TaskRegistry::RegisterTask(
            object_name, [](const nlohmann::json &params) {
                return nlohmann::json{{"received", params}};
            });
    elevate_kit::TaskRegistry::RegisterTask(
            array_name, [](const nlohmann::json &params) {
                return nlohmann::json{{"received", params}};
            });
    elevate_kit::TaskRegistry::RegisterTask(
            scalar_name, [](const nlohmann::json &params) {
                return nlohmann::json{{"received", params}};
            });

    const nlohmann::json object_params = {{"key", "value"}, {"number", 7}};
    const nlohmann::json array_params = {"first", 2, false};
    const nlohmann::json scalar_params = "scalar-value";

    CHECK(elevate_kit::TaskRegistry::RunTask(object_name, object_params) ==
          nlohmann::json{{"received", object_params}});
    CHECK(elevate_kit::TaskRegistry::RunTask(array_name, array_params) ==
          nlohmann::json{{"received", array_params}});
    CHECK(elevate_kit::TaskRegistry::RunTask(scalar_name, scalar_params) ==
          nlohmann::json{{"received", scalar_params}});
}
