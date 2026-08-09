#pragma once

#include "elevate_kit/export.h"

#include <string>

namespace elevate_kit {

class ApplicationId {
public:
    static void Set(const std::string &application_id);
    static std::string Get();

private:
    ApplicationId();
    static ApplicationId &Instance();

private:
    std::string application_id_;
};

}  // namespace elevate_kit
