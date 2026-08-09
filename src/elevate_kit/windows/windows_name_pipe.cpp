#include "../name_pipe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <nonstd/scope.hpp>
#ifdef SendMessage
#undef SendMessage
#endif
#ifdef LoadLibrary
#undef LoadLibrary
#endif

#include <fmt/format.h>
#include <fmt/xchar.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "common.h"

namespace elevate_kit {
namespace {

void CancelAndObserve(HANDLE pipe, OVERLAPPED &overlapped) {
    CancelIoEx(pipe, &overlapped);
    DWORD transferred = 0;
    GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
}

std::wstring PipeName(const std::string &application_id,
                      const std::string &session_id) {
    return fmt::format(LR"(\\.\pipe\{}-elevatekit-{})",
                       Utf8ToWide(application_id), Utf8ToWide(session_id));
}

class WindowsNamedPipe final : public INamedPipe {
public:
    explicit WindowsNamedPipe(
            nonstd::unique_resource<HANDLE, decltype(&CloseHandle)> &&pipe)
            : pipe_(std::move(pipe)) {}

public:
    bool Accept(std::chrono::milliseconds timeout) override {
        OVERLAPPED overlapped{};
        auto event = nonstd::make_unique_resource_checked(
                CreateEventW(nullptr, TRUE, FALSE, nullptr), nullptr,
                CloseHandle);
        if (event.get() == nullptr || event.get() == INVALID_HANDLE_VALUE) {
            return false;
        }
        overlapped.hEvent = event.get();
        if (ConnectNamedPipe(pipe_.get(), &overlapped) != 0) {
            return true;
        }
        DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            return true;
        }
        if (error != ERROR_IO_PENDING) {
            return false;
        }
        if (WaitForSingleObject(event.get(), timeout.count()) !=
            WAIT_OBJECT_0) {
            CancelAndObserve(pipe_.get(), overlapped);
            return false;
        }
        DWORD transferred = 0;
        if (GetOverlappedResult(pipe_.get(), &overlapped, &transferred,
                                FALSE) == FALSE) {
            CancelAndObserve(pipe_.get(), overlapped);
            return false;
        }
        return true;
    }

    bool SendMessage(const std::string &payload) override {
        if (payload.size() > (std::numeric_limits<uint32_t>::max)()) {
            return false;
        }
        auto length = static_cast<uint32_t>(payload.size());
        return WriteExact(pipe_.get(), &length, sizeof(length)) &&
               WriteExact(pipe_.get(), payload.data(),
                          static_cast<DWORD>(payload.size()));
    }

    std::optional<std::string> RecvMessage() override {
        uint32_t length = 0;
        if (!ReadExact(pipe_.get(), &length, sizeof(length))) {
            return std::nullopt;
        }
        std::string payload(length, '\0');
        if (!ReadExact(pipe_.get(), payload.data(),
                       static_cast<DWORD>(payload.size()))) {
            return std::nullopt;
        }
        return payload;
    }

    [[nodiscard]] uint64_t GetRemoteProcessId() const override {
        DWORD client_process_id = 0;
        DWORD server_process_id = 0;
        if (GetNamedPipeClientProcessId(pipe_.get(), &client_process_id) ==
                    FALSE ||
            GetNamedPipeServerProcessId(pipe_.get(), &server_process_id) ==
                    FALSE) {
            return 0;
        }
        if (client_process_id == server_process_id) {
            return client_process_id;
        }
        return client_process_id == GetCurrentProcessId() ? server_process_id
                                                          : client_process_id;
    }

private:
    bool ReadExact(HANDLE pipe, void *data, DWORD size) {
        auto *bytes = static_cast<unsigned char *>(data);
        DWORD offset = 0;
        while (offset < size) {
            OVERLAPPED overlapped{};
            auto event = nonstd::make_unique_resource_checked(
                    CreateEventW(nullptr, TRUE, FALSE, nullptr), nullptr,
                    CloseHandle);
            if (event.get() == nullptr || event.get() == INVALID_HANDLE_VALUE) {
                return false;
            }
            overlapped.hEvent = event.get();
            DWORD transferred = 0;
            if (ReadFile(pipe, bytes + offset, size - offset, &transferred,
                         &overlapped)) {
                if (transferred == 0) {
                    return false;
                }
            } else if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(event.get(), INFINITE) !=
                    WAIT_OBJECT_0) {
                    CancelAndObserve(pipe, overlapped);
                    return false;
                }
                if (!GetOverlappedResult(pipe, &overlapped, &transferred,
                                         FALSE)) {
                    CancelAndObserve(pipe, overlapped);
                    return false;
                }
                if (transferred == 0) {
                    return false;
                }
            } else {
                return false;
            }
            offset += transferred;
        }
        return true;
    }

    bool WriteExact(HANDLE pipe, const void *data, DWORD size) {
        const auto *bytes = static_cast<const unsigned char *>(data);
        DWORD offset = 0;
        while (offset < size) {
            OVERLAPPED overlapped{};
            auto event = nonstd::make_unique_resource_checked(
                    CreateEventW(nullptr, TRUE, FALSE, nullptr), nullptr,
                    CloseHandle);
            if (event.get() == nullptr || event.get() == INVALID_HANDLE_VALUE) {
                return false;
            }
            overlapped.hEvent = event.get();
            DWORD transferred = 0;
            if (WriteFile(pipe, bytes + offset, size - offset, &transferred,
                          &overlapped)) {
                if (transferred == 0) {
                    return false;
                }
            } else if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(event.get(), INFINITE) !=
                    WAIT_OBJECT_0) {
                    CancelAndObserve(pipe, overlapped);
                    return false;
                }
                if (!GetOverlappedResult(pipe, &overlapped, &transferred,
                                         FALSE)) {
                    CancelAndObserve(pipe, overlapped);
                    return false;
                }
                if (transferred == 0) {
                    return false;
                }
            } else {
                return false;
            }
            offset += transferred;
        }
        return true;
    }

private:
    nonstd::unique_resource<HANDLE, decltype(&CloseHandle)> pipe_;
};

}  // namespace

std::unique_ptr<INamedPipe> INamedPipe::Create(
        const std::string &application_id, const std::string &session_id) {
    std::wstring name = PipeName(application_id, session_id);
    HANDLE handle =
            CreateNamedPipeW(name.c_str(),
                             PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                     FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                     PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             1, 0, 0, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    auto pipe = nonstd::make_unique_resource_checked(
            handle, INVALID_HANDLE_VALUE, CloseHandle);
    return std::make_unique<WindowsNamedPipe>(std::move(pipe));
}

std::unique_ptr<INamedPipe> INamedPipe::Open(const std::string &application_id,
                                             const std::string &session_id) {
    std::wstring name = PipeName(application_id, session_id);
    if (!WaitNamedPipeW(name.c_str(), NMPWAIT_WAIT_FOREVER)) {
        return nullptr;
    }
    HANDLE handle =
            CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    auto pipe = nonstd::make_unique_resource_checked(
            handle, INVALID_HANDLE_VALUE, CloseHandle);
    return std::make_unique<WindowsNamedPipe>(std::move(pipe));
}

}  // namespace elevate_kit
