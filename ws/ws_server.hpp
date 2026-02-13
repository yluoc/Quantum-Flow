#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <functional>
#include <memory>

namespace quantumflow {

/// WebSocket server wrapping uWebSockets.
/// Runs the event loop in a background thread; broadcast() is thread-safe
/// via uWS::Loop::defer().
class WsServer {
public:
    using MessageHandler = std::function<void(const std::string&)>;

    WsServer();
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    /// Start listening on the given port. Returns true on success.
    bool init(int port = 9001);

    /// No-op kept for API compatibility (event loop runs in its own thread).
    void poll();

    /// Thread-safe broadcast: defers a text message send to the event loop thread.
    void broadcast(const std::string& message);
    void set_message_handler(MessageHandler handler);

    /// Graceful shutdown: close all connections, stop listening, join thread.
    void shutdown();

    /// Number of currently connected clients.
    size_t client_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quantumflow
