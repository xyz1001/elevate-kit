#include "elevate_kit/platform.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

TEST_CASE("platform process identity is available") {
    const std::uint64_t pid = elevate_kit::GetPid();
    const std::string process_path = elevate_kit::GetProcessPath(pid);

    CHECK(pid != 0);
    CHECK_FALSE(process_path.empty());
    CHECK(elevate_kit::GetProcessPath(
                  (std::numeric_limits<std::uint64_t>::max)())
                  .empty());
}

TEST_CASE("platform resolves module ids") {
    const std::string process_path =
            elevate_kit::GetProcessPath(elevate_kit::GetPid());

    CHECK(elevate_kit::GetModulePathFromAddress(nullptr) == process_path);
    CHECK_FALSE(elevate_kit::GetModulePathFromAddress(
                        reinterpret_cast<const void *>(&elevate_kit::GetPid))
                        .empty());
}

TEST_CASE("platform rejects a missing module") {
    const std::string process_path =
            elevate_kit::GetProcessPath(elevate_kit::GetPid());
    const std::filesystem::path missing_path =
            std::filesystem::path(process_path).parent_path() /
            "__elevate_kit_platform_test_missing_8f3c7a1d__";
    const std::string missing_module = missing_path.string();

    CHECK_FALSE(elevate_kit::LoadLibrary(missing_module));
}
