#include "elevate_worker.h"

#include "elevate_platform.h"

#include <string>

namespace elevate_kit::detail {
bool ElevateWorker::process(int argc, char *argv[]) {
    if (argc == 4 && std::string(argv[2]) == "--elevate-ipc") {
        (void) executeWorker(argv[3]);
    }
    return true;
}
}  // namespace elevate_kit::detail
