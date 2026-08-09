#include "elevate_kit/application_id.h"

#include "doctest/doctest.h"

#include <stdexcept>

TEST_CASE("ApplicationId stores and returns the application id") {
    CHECK(elevate_kit::ApplicationId::Get() == "unittest");

    elevate_kit::ApplicationId::Set("initial-application");
    CHECK(elevate_kit::ApplicationId::Get() == "initial-application");

    elevate_kit::ApplicationId::Set("overridden-application");
    CHECK(elevate_kit::ApplicationId::Get() == "overridden-application");
}

TEST_CASE("ApplicationId accepts valid ASCII characters") {
    elevate_kit::ApplicationId::Set("letters");
    CHECK(elevate_kit::ApplicationId::Get() == "letters");

    elevate_kit::ApplicationId::Set("LETTERS");
    CHECK(elevate_kit::ApplicationId::Get() == "LETTERS");

    elevate_kit::ApplicationId::Set("0123456789");
    CHECK(elevate_kit::ApplicationId::Get() == "0123456789");

    elevate_kit::ApplicationId::Set(".");
    CHECK(elevate_kit::ApplicationId::Get() == ".");

    elevate_kit::ApplicationId::Set("_");
    CHECK(elevate_kit::ApplicationId::Get() == "_");

    elevate_kit::ApplicationId::Set("-");
    CHECK(elevate_kit::ApplicationId::Get() == "-");

    elevate_kit::ApplicationId::Set("a1.B_2-c");
    CHECK(elevate_kit::ApplicationId::Get() == "a1.B_2-c");
}

TEST_CASE("ApplicationId rejects invalid values without updating") {
    elevate_kit::ApplicationId::Set("valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set(""), std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set("has space"),
                    std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set("has/slash"),
                    std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set("has\\backslash"),
                    std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set("has@symbol"),
                    std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");

    CHECK_THROWS_AS(elevate_kit::ApplicationId::Set("has\tcontrol"),
                    std::invalid_argument);
    CHECK(elevate_kit::ApplicationId::Get() == "valid-id");
}
