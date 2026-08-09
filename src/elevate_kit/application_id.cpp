#include "application_id.h"

#include <cctype>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "platform.h"

namespace elevate_kit {

ApplicationId::ApplicationId()
        : application_id_(
                  std::filesystem::path(::elevate_kit::GetProcessPath(GetPid()))
                          .filename()
                          .string()) {}

ApplicationId &ApplicationId::Instance() {
    static ApplicationId instance;
    return instance;
}

void ApplicationId::Set(const std::string &application_id) {
    if (application_id.empty()) {
        throw std::invalid_argument("application id must not be empty");
    }

    if (!std::all_of(application_id.begin(), application_id.end(),
                     [](unsigned char ch) {
                         return std::isalnum(ch) || ch == '.' || ch == '_' ||
                                ch == '-';
                     })) {
        throw std::invalid_argument(
                "application id contains an invalid character");
    }

    Instance().application_id_ = application_id;
}

std::string ApplicationId::Get() {
    return Instance().application_id_;
}

}  // namespace elevate_kit
