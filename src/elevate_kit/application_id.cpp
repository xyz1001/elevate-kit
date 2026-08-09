#include "application_id.h"

namespace elevate_kit::detail {
namespace {
std::string configured_application_id;
}  // namespace

void setConfiguredApplicationId(const std::string &application_id) {
    configured_application_id = application_id;
}

const std::string &configuredApplicationId() {
    return configured_application_id;
}

}  // namespace elevate_kit::detail
