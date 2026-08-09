#include "elevate_kit/elevate_kit.h"

#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
#endif

REGISTER_ELEVATED_TASK("Example_Task",
                       [](const std::string &) { return true; });

int main(int argc, char *argv[]) {
    const bool is_worker = argc >= 2 && argv[1] != nullptr &&
                           std::string(argv[1]) == "--elevated";
#ifdef _WIN32
    if (is_worker) {
        FreeConsole();
    }
#endif

    if (is_worker) {
        // Worker diagnostics are unavailable after detaching from the console.
    } else if (argc < 2) {
        fmt::print(stderr, "example: ordinary startup\n");
    } else if (std::string(argv[1]) == "--elevated-install") {
        fmt::print(stderr, "example: installer startup\n");
    } else {
        fmt::print(stderr, "example: ordinary startup\n");
    }
    if (elevate_kit::ElevateKit::process(argc, argv)) {
        if (argc >= 2 && std::string(argv[1]) == "--elevated-install") {
            const bool ready = elevate_kit::ElevateKit::isPasswordlessReady();
            fmt::print(stderr, "example: installer readiness={} exit={}\n",
                       ready, ready ? 0 : 1);
            return ready ? 0 : 1;
        }
        return 0;
    }
    const bool success = elevate_kit::ElevateKit::runTask("Example_Task", "{}");
    fmt::print(stderr, "example: ordinary runTask success={} exit={}\n",
               success, success ? 0 : 1);
    return success ? 0 : 1;
}
