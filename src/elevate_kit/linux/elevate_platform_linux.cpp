#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../elevate_platform.h"

#include "../application_id.h"

#include "../task_registry.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace elevate_kit::detail {
namespace {

constexpr auto limit = std::chrono::seconds(3);

using Clock = std::chrono::steady_clock;
Clock::time_point deadline() {
    return Clock::now() + limit;
}
int remaining(Clock::time_point d) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            d - Clock::now())
                            .count();
    return ms <= 0 ? 0 : static_cast<int>(ms);
}

std::string hostPath() {
    std::array<char, 4096> b{};
    return realpath("/proc/self/exe", b.data()) ? std::string(b.data())
                                                : std::string();
}

std::string normalizedApplicationId(const std::string &host) {
    std::string value = configuredApplicationId();
    if (value.empty()) {
        const size_t separator = host.find_last_of("/\\");
        value = separator == std::string::npos ? host
                                               : host.substr(separator + 1);
        if (value.size() >= 4 && value[value.size() - 4] == '.' &&
            (value[value.size() - 3] == 'e' ||
             value[value.size() - 3] == 'E') &&
            (value[value.size() - 2] == 'x' ||
             value[value.size() - 2] == 'X') &&
            (value[value.size() - 1] == 'e' ||
             value[value.size() - 1] == 'E')) {
            value.resize(value.size() - 4);
        }
    }

    std::string normalized;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-') {
            normalized += static_cast<char>(c);
        } else {
            normalized += '-';
        }
    }
    return normalized.empty() ? "host" : normalized;
}

struct Names {
    std::string action;
    std::string policy_file;
    std::string rule_file;
};

Names namesForHost(const std::string &host) {
    const std::string id = normalizedApplicationId(host);
    Names names;
    names.action = "com.elevatekit." + id + ".elevated";
    names.policy_file =
            "/usr/share/polkit-1/actions/" + names.action + ".policy";
    names.rule_file = "/etc/polkit-1/rules.d/49-" + names.action + ".rules";
    return names;
}

bool releasePathOK(const std::string &p) {
#ifdef NDEBUG
    return p.compare(0, std::strlen("/usr/libexec/"), "/usr/libexec/") == 0;
#else
    (void) p;
    return true;
#endif
}

bool randomId(std::string &id) {
    std::array<unsigned char, 16> bytes{};
    if (getrandom(bytes.data(), bytes.size(), 0) !=
        static_cast<ssize_t>(bytes.size()))
        return false;
    static constexpr char hex[] = "0123456789abcdef";
    id.clear();
    for (unsigned char c : bytes) {
        id += hex[c >> 4];
        id += hex[c & 15];
    }
    return true;
}

std::string endpoint(const std::string &id) {
    return "/tmp/elevate-kit-" + id + ".sock";
}

bool waitFd(int fd, short events, Clock::time_point d) {
    pollfd p{fd, events, 0};
    while (remaining(d) > 0) {
        const int r = poll(&p, 1, remaining(d));
        if (r > 0) {
            if (p.revents & events) return true;
            return false;
        }
        if (r < 0 && errno == EINTR) continue;
        return false;
    }
    return false;
}

bool writeAll(int fd, const void *data, size_t n, Clock::time_point d) {
    const char *p = static_cast<const char *>(data);
    while (n) {
        if (!waitFd(fd, POLLOUT, d)) return false;
        const ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
        if (r > 0) {
            p += r;
            n -= static_cast<size_t>(r);
        } else if (r < 0 &&
                   (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        else
            return false;
    }
    return true;
}

bool readAll(int fd, void *data, size_t n, Clock::time_point d) {
    char *p = static_cast<char *>(data);
    while (n) {
        if (!waitFd(fd, POLLIN, d)) return false;
        const ssize_t r = read(fd, p, n);
        if (r > 0) {
            p += r;
            n -= static_cast<size_t>(r);
        } else if (r < 0 &&
                   (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        else
            return false;
    }
    return true;
}

bool sendFrame(int fd, const std::string &payload, Clock::time_point d) {
    if (payload.size() > UINT32_MAX) return false;
    const auto h = encodePayloadLength(static_cast<uint32_t>(payload.size()));
    return writeAll(fd, h.data(), h.size(), d) &&
           writeAll(fd, payload.data(), payload.size(), d);
}
bool receiveFrame(int fd, std::string &payload, Clock::time_point d) {
    std::array<uint8_t, 4> h{};
    if (!readAll(fd, h.data(), h.size(), d)) return false;
    const uint32_t n = decodePayloadLength(h);
    payload.resize(n);
    return n == 0 || readAll(fd, payload.data(), n, d);
}

bool connectSocket(int fd, const sockaddr_un &address, Clock::time_point d) {
    if (connect(fd, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == 0)
        return true;
    if (errno != EINPROGRESS) return false;
    if (!waitFd(fd, POLLOUT, d)) return false;
    int error = 0;
    socklen_t length = sizeof(error);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
           error == 0;
}

bool runAndWait(const std::vector<std::string> &args) {
    std::vector<char *> av;
    for (const auto &a : args) av.push_back(const_cast<char *>(a.c_str()));
    av.push_back(nullptr);
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        execv(av[0], av.data());
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(p, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != p) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string xmlEscape(const std::string &value) {
    std::string result;
    for (const char c : value) {
        if (c == '&')
            result += "&amp;";
        else if (c == '<')
            result += "&lt;";
        else if (c == '>')
            result += "&gt;";
        else if (c == '"')
            result += "&quot;";
        else if (c == '\'')
            result += "&apos;";
        else
            result += c;
    }
    return result;
}

std::string policyText(const std::string &host, const Names &names) {
    return std::string(
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE "
                   "policyconfig PUBLIC \"-//freedesktop//DTD PolicyKit Policy "
                   "Configuration 1.0//EN\" "
                   "\"http://www.freedesktop.org/standards/PolicyKit/1.0/"
                   "policyconfig.dtd\">\n<policyconfig><action id=\"") +
           names.action +
           "\"><description>Elevate Kit</description><message>Elevate Kit "
           "requires "
           "authorization</message><defaults><allow_any>auth_admin</"
           "allow_any><allow_inactive>auth_admin</"
           "allow_inactive><allow_active>auth_admin</allow_active></"
           "defaults><annotate key=\"org.freedesktop.policykit.exec.path\">" +
           xmlEscape(host) +
           "</annotate><annotate "
           "key=\"org.freedesktop.policykit.exec.argv1\">--elevated</"
           "annotate></action></policyconfig>\n";
}

std::string ruleText(const Names &names) {
    return "polkit.addRule(function(action) { if (action.id == \"" +
           names.action + "\") return polkit.Result.YES; });\n";
}

bool expectedPolicy(const std::string &host, const Names &names) {
    std::ifstream p(names.policy_file);
    if (!p) return false;
    const std::string ps((std::istreambuf_iterator<char>(p)), {});
    return ps == policyText(host, names);
}

bool expectedFiles(const std::string &host, const Names &names) {
    if (!expectedPolicy(host, names)) return false;
    std::ifstream r(names.rule_file);
    if (!r) return false;
    const std::string rs((std::istreambuf_iterator<char>(r)), {});
    return rs == ruleText(names);
}

}  // namespace

bool installOrRepair() {
    if (geteuid() != 0) return false;
    const std::string host = hostPath();
    if (host.empty() || !releasePathOK(host)) return false;
    const Names names = namesForHost(host);
    const std::string policy = policyText(host, names);
    const std::string rule = ruleText(names);
    {
        std::ofstream out(names.policy_file, std::ios::trunc);
        if (!out) return false;
        out << policy;
        if (!out) return false;
    }
    {
        std::ofstream out(names.rule_file, std::ios::trunc);
        if (!out) return false;
        out << rule;
        if (!out) return false;
    }
    chmod(names.policy_file.c_str(), 0644);
    chmod(names.rule_file.c_str(), 0644);
    return expectedFiles(host, names);
}

bool isPasswordlessReady() {
    const std::string host = hostPath();
    if (host.empty() || !releasePathOK(host)) return false;
    const Names names = namesForHost(host);
    if (!expectedPolicy(host, names)) return false;
    const std::vector<std::string> args{"/usr/bin/pkcheck", "--action-id",
                                        names.action, "--process",
                                        std::to_string(getpid())};
    return runAndWait(args);
}

bool executeWorker(const std::string &ipc_id) {
    if (geteuid() != 0 || ipc_id.empty()) return false;
    const std::string host = hostPath(), path = endpoint(ipc_id);
    const char *uidText = std::getenv("PKEXEC_UID");
    if (host.empty() || !uidText) return false;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(uidText, &end, 10);
    if (!end || *end || parsed > static_cast<unsigned long>(UINT_MAX))
        return false;
    const uid_t expected = static_cast<uid_t>(parsed);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    if (path.size() >= sizeof(a.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
    const auto d = deadline();
    if (!connectSocket(fd, a, d)) {
        close(fd);
        return false;
    }
    ucred peer{};
    socklen_t plen = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &plen) < 0 ||
        peer.uid != expected) {
        close(fd);
        return false;
    }
    std::string exe = "/proc/" + std::to_string(peer.pid) + "/exe";
    std::array<char, 4096> b{};
    const ssize_t n = readlink(exe.c_str(), b.data(), b.size() - 1);
    if (n <= 0 || std::string(b.data(), static_cast<size_t>(n)) != host) {
        close(fd);
        return false;
    }
    std::string payload;
    Request req;
    bool ok = receiveFrame(fd, payload, d) &&
              deserializeRequest(payload, req) && req.request_id == ipc_id;
    if (ok) ok = runTask(req.task_name, req.params_json);
    std::string response;
    if (!serializeResponse(ok, response) || !sendFrame(fd, response, d))
        ok = false;
    close(fd);
    return ok;
}

bool executeRequest(const Request &request) {
    if (!isPasswordlessReady()) {
        const std::string host = hostPath();
        if (host.empty() ||
            !runAndWait({"/usr/bin/pkexec", host, "--elevated-install"}) ||
            !isPasswordlessReady())
            return false;
    }
    std::string id;
    if (!randomId(id)) return false;
    const std::string path = endpoint(id);
    unlink(path.c_str());
    const int server =
            socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server < 0) return false;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    if (path.size() >= sizeof(a.sun_path)) {
        close(server);
        return false;
    }
    std::strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
    bool good =
            bind(server, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0 &&
            listen(server, 1) == 0;
    if (!good) {
        close(server);
        unlink(path.c_str());
        return false;
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        close(server);
        unlink(path.c_str());
        return false;
    }
    const auto d = deadline();
    pid_t launcher = fork();
    if (launcher == 0) {
        close(pipefd[0]);
        pid_t worker = fork();
        if (worker > 0) {
            (void) write(pipefd[1], &worker, sizeof(worker));
            close(pipefd[1]);
            _exit(0);
        }
        if (worker == 0) {
            close(pipefd[1]);
            const std::string host = hostPath();
            const std::string arg0 = "/usr/bin/pkexec", arg1 = host,
                              arg2 = "--elevated", arg3 = "--elevate-ipc",
                              arg4 = id;
            char *av[] = {const_cast<char *>(arg0.c_str()),
                          const_cast<char *>(arg1.c_str()),
                          const_cast<char *>(arg2.c_str()),
                          const_cast<char *>(arg3.c_str()),
                          const_cast<char *>(arg4.c_str()),
                          nullptr};
            execv(av[0], av);
            _exit(127);
        }
        pid_t bad = -1;
        (void) write(pipefd[1], &bad, sizeof(bad));
        _exit(127);
    }
    close(pipefd[1]);
    pid_t worker = -1;
    if (launcher > 0) readAll(pipefd[0], &worker, sizeof(worker), d);
    close(pipefd[0]);
    if (launcher > 0) {
        int s = 0;
        bool reaped = false;
        while (remaining(d) > 0) {
            const pid_t r = waitpid(launcher, &s, WNOHANG);
            if (r == launcher) {
                reaped = true;
                break;
            }
            if (r < 0 && errno != EINTR) break;
            (void) poll(nullptr, 0, 1);
        }
        if (!reaped) {
            (void) kill(launcher, SIGKILL);
            while (waitpid(launcher, &s, 0) < 0 && errno == EINTR) {
            }
        }
    }
    bool result = false;
    int client = -1;
    if (worker > 0 && waitFd(server, POLLIN, d)) {
        ucred peer{};
        socklen_t plen = sizeof(peer);
        client =
                accept4(server, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client >= 0 &&
            getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &plen) == 0 &&
            peer.uid == 0 && peer.pid == worker) {
            Request req = request;
            req.request_id = id;
            std::string payload;
            const bool transport_ok = serializeRequest(req, payload) &&
                                      sendFrame(client, payload, d) &&
                                      receiveFrame(client, payload, d);
            bool response_success = false;
            const bool parse_ok =
                    transport_ok &&
                    deserializeResponse(payload, response_success);
            result = parse_ok && response_success;
        }
    }
    if (client >= 0) {
        close(client);
    }
    close(server);
    unlink(path.c_str());
    return result;
}

}  // namespace elevate_kit::detail
