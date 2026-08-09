#include "elevate_kit.h"

#include "application_id.h"
#include "elevate_client.h"
#include "elevate_platform.h"
#include "elevate_worker.h"

namespace elevate_kit {

void ElevateKit::setApplicationId(const std::string &application_id) {
    detail::setConfiguredApplicationId(application_id);
}

bool ElevateKit::process(int argc, char *argv[]) {
    if (argc < 2) {
        return false;
    }
    if (std::string(argv[1]) == "--elevated") {
        return detail::ElevateWorker::process(argc, argv);
    }
    if (std::string(argv[1]) == "--elevated-install") {
        (void) detail::installOrRepair();
        return true;
    }
    return false;
}

bool ElevateKit::runTask(const std::string &task_name,
                         const std::string &params_json,
                         const std::string &module_id) {
    return detail::ElevateClient::runTask(task_name, params_json, module_id);
}

bool ElevateKit::isPasswordlessReady() {
    return detail::ElevateClient::isPasswordlessReady();
}

}  // namespace elevate_kit
