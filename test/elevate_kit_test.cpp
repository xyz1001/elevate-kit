#include "elevate_kit/elevate_kit.h"

#include "elevate_kit/application_id.h"

#include "doctest/doctest.h"

namespace {
constexpr const char *kFinalApplicationId = "elevate_kit_test_final";
}

TEST_CASE("SetApplicationId forwards and supports repeated overrides") {
    elevate_kit::SetApplicationId("elevate_kit_test_first");
    CHECK(elevate_kit::ApplicationId::Get() == "elevate_kit_test_first");

    elevate_kit::SetApplicationId("elevate_kit_test_second");
    CHECK(elevate_kit::ApplicationId::Get() == "elevate_kit_test_second");

    elevate_kit::SetApplicationId(kFinalApplicationId);
    CHECK(elevate_kit::ApplicationId::Get() == kFinalApplicationId);
}

TEST_CASE("Process returns immediately with no or one argument") {
    elevate_kit::SetApplicationId(kFinalApplicationId);

    elevate_kit::Process(0, nullptr);

    char program[] = "unittest";
    char *one_argument_argv[] = {program, nullptr};
    elevate_kit::Process(1, one_argument_argv);

    CHECK(elevate_kit::ApplicationId::Get() == kFinalApplicationId);
}

TEST_CASE("Process returns for invalid and incomplete CLI input") {
    elevate_kit::SetApplicationId(kFinalApplicationId);

    char program[] = "unittest";
    char invalid_option[] = "--not-an-elevate-kit-option";
    char *invalid_argv[] = {program, invalid_option, nullptr};
    elevate_kit::Process(2, invalid_argv);

    char install_option[] = "--elevate-install";
    char *missing_required_argv[] = {program, install_option, nullptr};
    elevate_kit::Process(2, missing_required_argv);

    CHECK(elevate_kit::ApplicationId::Get() == kFinalApplicationId);
}
