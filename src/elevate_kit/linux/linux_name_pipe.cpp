#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../name_pipe.h"

#include <chrono>
#include <climits>
#include <cstdint>

#include <optional>
#include <string>

#include <fmt/format.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <nonstd/scope.hpp>

namespace elevate_kit {

class LinuxNamedPipe final : public INamedPipe {
public:
    explicit LinuxNamedPipe(nonstd::unique_resource<int, decltype(&close)> &&fd)
            : fd_(std::move(fd)) {}

    ~LinuxNamedPipe() override {
        sockaddr_un address{};
        socklen_t length = sizeof(address);
        socklen_t path_offset = offsetof(sockaddr_un, sun_path);
        if (getsockname(fd_.get(), reinterpret_cast<sockaddr *>(&address),
                        &length) == 0 &&
            length > path_offset && length <= sizeof(address) &&
            address.sun_path[0] != '\0') {
            size_t path_length =
                    strnlen(address.sun_path, length - path_offset);
            unlink(std::string(address.sun_path, path_length).c_str());
        }
    }

public:
    bool Accept(std::chrono::milliseconds timeout) override {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        pollfd descriptor{fd_.get(), POLLIN, 0};
        while (true) {
            auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now());
            int milliseconds = remaining.count() <= 0
                                       ? 0
                                       : static_cast<int>(remaining.count());
            int ready = poll(&descriptor, 1, milliseconds);
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            if (ready <= 0) {
                return false;
            }
            if ((descriptor.revents & POLLIN) == 0) {
                return false;
            }
            break;
        }
        int client = 0;
        do {
            client = accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
        } while (client < 0 && errno == EINTR);
        if (client < 0) {
            return false;
        }
        fd_.reset(client);
        return true;
    }

    bool SendMessage(const std::string &payload) override {
        if (payload.size() > UINT32_MAX) {
            return false;
        }
        auto length = static_cast<uint32_t>(payload.size());
        return WriteAll(fd_.get(), &length, sizeof(length)) &&
               WriteAll(fd_.get(), payload.data(), payload.size());
    }

    std::optional<std::string> RecvMessage() override {
        uint32_t n = 0;
        if (!ReadAll(fd_.get(), &n, sizeof(n))) {
            return std::nullopt;
        }
        std::string payload(n, '\0');
        if (n != 0 && !ReadAll(fd_.get(), payload.data(), n)) {
            return std::nullopt;
        }
        return payload;
    }

    std::uint64_t GetRemoteProcessId() const override {
        ucred peer{};
        socklen_t length = sizeof(peer);
        if (getsockopt(fd_.get(), SOL_SOCKET, SO_PEERCRED, &peer, &length) <
            0) {
            return 0;
        }
        return peer.pid > 0 ? static_cast<std::uint64_t>(peer.pid) : 0;
    }

private:
    static bool WriteAll(int fd, const void *data, size_t n) {
        const char *p = static_cast<const char *>(data);
        while (n != 0U) {
            ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
            if (r > 0) {
                p += r;
                n -= static_cast<size_t>(r);
            } else if (r < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    static bool ReadAll(int fd, void *data, size_t n) {
        char *p = static_cast<char *>(data);
        while (n != 0U) {
            ssize_t r = read(fd, p, n);
            if (r > 0) {
                p += r;
                n -= static_cast<size_t>(r);
            } else if (r < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

private:
    nonstd::unique_resource<int, decltype(&close)> fd_;
};

std::string GetPipePath(const std::string &application_id,
                        const std::string &session_id) {
    return fmt::format("/tmp/{}-elevatekit-{}.sock", application_id,
                       session_id);
}

std::unique_ptr<INamedPipe> INamedPipe::Create(
        const std::string &application_id, const std::string &session_id) {
    std::string path = GetPipePath(application_id, session_id);
    unlink(path.c_str());
    int handle = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (handle < 0) {
        return nullptr;
    }
    auto fd = nonstd::make_unique_resource_checked(handle, -1, close);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        return nullptr;
    }
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (bind(fd.get(), reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0) {
        return nullptr;
    }
    if (listen(fd.get(), 1) != 0) {
        unlink(path.c_str());
        return nullptr;
    }
    return std::make_unique<LinuxNamedPipe>(std::move(fd));
}

std::unique_ptr<INamedPipe> INamedPipe::Open(const std::string &application_id,
                                             const std::string &session_id) {
    int handle = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (handle < 0) {
        return nullptr;
    }
    auto fd = nonstd::make_unique_resource_checked(handle, -1, close);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::string path = GetPipePath(application_id, session_id);
    if (path.size() >= sizeof(address.sun_path)) {
        return nullptr;
    }
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    while (connect(fd.get(), reinterpret_cast<const sockaddr *>(&address),
                   sizeof(address)) < 0) {
        if (errno != EINTR) {
            return nullptr;
        }
    }
    unlink(path.c_str());
    return std::make_unique<LinuxNamedPipe>(std::move(fd));
}

}  // namespace elevate_kit
