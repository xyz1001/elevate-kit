#pragma once

#include <string>

namespace elevate_kit::detail {

class ElevateClient {
public:
    static bool runTask(const std::string &task_name,
                        const std::string &params_json,
                        const std::string &module_id);
    static bool isPasswordlessReady();
};

}  // namespace elevate_kit::detail
