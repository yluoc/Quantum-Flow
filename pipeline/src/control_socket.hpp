// Runtime symbol-update listener over an AF_UNIX datagram socket.
// Mirrors the control_listener task in app.py: receives JSON
// {"type":"set_symbols","symbols":[...]} and applies it.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace quantumflow::pipeline {

/// Trim, drop empties, and de-duplicate (preserving order) a symbol list.
std::vector<std::string> normalize_symbols(const std::vector<std::string>& symbols);

class ControlSocket {
public:
    using SymbolsCallback = std::function<void(std::vector<std::string>)>;

    ControlSocket(std::string socket_path, SymbolsCallback on_symbols);
    ~ControlSocket();

    ControlSocket(const ControlSocket&) = delete;
    ControlSocket& operator=(const ControlSocket&) = delete;

    /// Bind the socket and start the listener thread. No-op if path is empty.
    void start();

    /// Signal shutdown, join the thread, and unlink the socket file.
    void stop();

private:
    void run();

    std::string socket_path_;
    SymbolsCallback on_symbols_;
    std::atomic<bool> stop_{false};
    int fd_ = -1;
    std::thread thread_;
};

} // namespace quantumflow::pipeline
