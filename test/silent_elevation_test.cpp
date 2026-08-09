#include "elevate_kit/silent_elevation.h"

#include "elevate_kit/platform.h"

#include "doctest/doctest.h"

#include <string>

#ifdef __linux__
#include <unistd.h>
#endif

TEST_CASE("silent elevation reports local readiness without changing it") {
    auto elevation = elevate_kit::ISilentElevation::Create();
    REQUIRE(elevation);

    const bool elevated = elevation->IsElevated();
#ifdef __linux__
    CHECK(elevated == (geteuid() == 0));
#else
    // The Windows implementation has no portable equivalent of geteuid().
    CHECK(elevation->IsElevated() == elevated);
#endif

    const std::string application_id =
            "silent_test_" + std::to_string(elevate_kit::GetPid());
    CHECK_FALSE(elevation->IsSilentElevationConfigured(application_id));
}
