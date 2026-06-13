#include "control_socket.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "common/unix_socket.hpp"

namespace quantumflow::pipeline {

std::vector<std::string> normalize_symbols(const std::vector<std::string>& symbols) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& raw : symbols) {
        // Trim ASCII whitespace.
        size_t b = raw.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = raw.find_last_not_of(" \t\r\n");
        std::string sym = raw.substr(b, e - b + 1);
        if (sym.empty() || seen.count(sym)) continue;
        seen.insert(sym);
        out.push_back(std::move(sym));
    }
    return out;
}

ControlSocket::ControlSocket(std::string socket_path, SymbolsCallback on_symbols)
    : socket_path_(std::move(socket_path)), on_symbols_(std::move(on_symbols)) {}

ControlSocket::~ControlSocket() {
    stop();
}

void ControlSocket::start() {
    if (socket_path_.empty()) return;

    if (!unix_path_fits(socket_path_)) {
        std::fprintf(stderr, "[control] socket path may be too long for AF_UNIX: %s\n",
                     socket_path_.c_str());
    }

    fd_ = make_unix_dgram_socket();
    if (fd_ < 0) {
        std::fprintf(stderr, "[control] failed to create socket: %s\n", std::strerror(errno));
        return;
    }

    if (!bind_unix_dgram(fd_, socket_path_)) {
        std::fprintf(stderr, "[control] failed to bind %s: %s\n", socket_path_.c_str(),
                     std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return;
    }

    std::fprintf(stderr, "[control] listening for symbol updates on %s\n", socket_path_.c_str());
    thread_ = std::thread([this] { run(); });
}

void ControlSocket::run() {
    char data[4096];
    while (!stop_.load(std::memory_order_relaxed)) {
        pollfd pfd{fd_, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, 500);  // 0.5s timeout, like the Python loop.
        if (rc <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        const ssize_t n = ::recvfrom(fd_, data, sizeof(data), 0, nullptr, nullptr);
        if (n <= 0) continue;

        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(data, data + n);
        } catch (const nlohmann::json::parse_error&) {
            continue;
        }
        if (!msg.is_object() || msg.value("type", std::string{}) != "set_symbols") continue;

        const auto it = msg.find("symbols");
        if (it == msg.end() || !it->is_array()) continue;

        std::vector<std::string> incoming;
        for (const auto& s : *it) {
            if (s.is_string()) incoming.push_back(s.get<std::string>());
            else incoming.push_back(s.dump());  // str(s), mirroring Python's str() coercion.
        }
        std::vector<std::string> next = normalize_symbols(incoming);
        if (next.empty()) continue;

        if (on_symbols_) on_symbols_(std::move(next));
    }
}

void ControlSocket::stop() {
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!socket_path_.empty()) ::unlink(socket_path_.c_str());
}

} // namespace quantumflow::pipeline
