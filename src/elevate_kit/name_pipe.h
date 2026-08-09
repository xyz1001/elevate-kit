#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace elevate_kit {
class INamedPipe {
public:
    virtual ~INamedPipe() = default;
    static std::unique_ptr<INamedPipe> Create(const std::string &application_id,
                                              const std::string &session_id);
    static std::unique_ptr<INamedPipe> Open(const std::string &application_id,
                                            const std::string &session_id);

public:
    virtual bool Accept(std::chrono::milliseconds timeout) = 0;
    virtual bool SendMessage(const std::string &payload) = 0;
    virtual std::optional<std::string> RecvMessage() = 0;
    [[nodiscard]] virtual uint64_t GetRemoteProcessId() const = 0;
};
}  // namespace elevate_kit
