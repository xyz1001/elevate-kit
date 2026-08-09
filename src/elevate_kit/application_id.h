#pragma once

#include "elevate_kit/export.h"

#include <string>

namespace elevate_kit::detail {

ELEVATE_KIT_API void setConfiguredApplicationId(
        const std::string &application_id);
ELEVATE_KIT_API const std::string &configuredApplicationId();

}  // namespace elevate_kit::detail
