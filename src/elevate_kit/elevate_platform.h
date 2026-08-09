#pragma once

#include "elevate_protocol.h"

#include <string>

namespace elevate_kit::detail {

bool executeRequest(const Request &request);
bool executeWorker(const std::string &ipc_id);
bool installOrRepair();
bool isPasswordlessReady();

}  // namespace elevate_kit::detail
