#include "elevate_client.h"

#include "elevate_platform.h"

namespace elevate_kit::detail {

bool ElevateClient::runTask(const std::string &task_name,
                            const std::string &params_json,
                            const std::string &module_id) {
    const Request request{task_name, params_json, module_id, ""};
    return executeRequest(request);
}

bool ElevateClient::isPasswordlessReady() {
    return elevate_kit::detail::isPasswordlessReady();
}

}  // namespace elevate_kit::detail
