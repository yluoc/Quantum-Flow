#include "ws/ws_server.hpp"

#include <App.h> // uWebSockets
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdio>

namespace quantumflow {

struct WsServer::Impl {
    struct PerSocketData {};

    uWS::App* app = nullptr;
    uWS::Loop* loop = nullptr;
    us_listen_socket_t* listen_socket = nullptr;
    std::vector<uWS::WebSocket<false, true, PerSocketData>*> clients;

    std::thread event_thread;
    std::atomic<bool> running{false};
    std::atomic<size_t> client_count{0};

    // Synchronisation for init: main thread waits until listen completes
    std::mutex init_mutex;
    std::condition_variable init_cv;
    bool init_done = false;
    bool init_ok = false;
};

WsServer::WsServer() : impl_(new Impl) {}

WsServer::~WsServer() {
    shutdown();
    delete impl_;
}

bool WsServer::init(int port) {
    // Launch the event loop in a background thread
    impl_->event_thread = std::thread([this, port]() {
        // uWS::Loop::get() is per-thread; this creates the loop for this thread
        impl_->loop = uWS::Loop::get();

        impl_->app = new uWS::App();

        impl_->app->ws<Impl::PerSocketData>("/*", {
            .compression = uWS::DISABLED,
            .maxPayloadLength = 64 * 1024,
            .idleTimeout = 120,
            .maxBackpressure = 1 * 1024 * 1024,

            .open = [this](auto* ws) {
                impl_->clients.push_back(ws);
                impl_->client_count.store(impl_->clients.size(),
                                          std::memory_order_relaxed);
                std::printf("[WsServer] Client connected (%zu total)\n",
                            impl_->clients.size());
            },

            .message = [](auto* /*ws*/, std::string_view /*message*/,
                          uWS::OpCode /*opCode*/) {
                // Server is broadcast-only; ignore incoming messages
            },

            .close = [this](auto* ws, int /*code*/,
                            std::string_view /*message*/) {
                auto& c = impl_->clients;
                c.erase(std::remove(c.begin(), c.end(), ws), c.end());
                impl_->client_count.store(c.size(),
                                          std::memory_order_relaxed);
                std::printf("[WsServer] Client disconnected (%zu remaining)\n",
                            c.size());
            }
        });

        impl_->app->listen(port, [this, port](us_listen_socket_t* socket) {
            std::lock_guard<std::mutex> lock(impl_->init_mutex);
            if (socket) {
                impl_->listen_socket = socket;
                impl_->running.store(true);
                impl_->init_ok = true;
                std::printf("[WsServer] Listening on port %d\n", port);
            } else {
                std::fprintf(stderr,
                             "[WsServer] Failed to listen on port %d\n", port);
                impl_->init_ok = false;
            }
            impl_->init_done = true;
            impl_->init_cv.notify_one();
        });

        // Blocks here until shutdown closes the listen socket
        impl_->app->run();

        // Cleanup after run() returns
        delete impl_->app;
        impl_->app = nullptr;
    });

    // Wait for the listen call to complete in the event thread
    {
        std::unique_lock<std::mutex> lock(impl_->init_mutex);
        impl_->init_cv.wait(lock, [this] { return impl_->init_done; });
    }

    return impl_->init_ok;
}

void WsServer::poll() {
    // No-op: the event loop runs in its own thread
}

void WsServer::broadcast(const std::string& message) {
    if (!impl_->running.load() || !impl_->loop) return;

    // Capture by value so the string outlives this call
    impl_->loop->defer([this, msg = message]() {
        for (auto* ws : impl_->clients) {
            ws->send(msg, uWS::OpCode::TEXT, false);
        }
    });
}

void WsServer::shutdown() {
    if (!impl_->running.load()) return;
    impl_->running.store(false);

    if (impl_->loop) {
        // Defer closing into the event loop thread
        impl_->loop->defer([this]() {
            // Close all client connections
            for (auto* ws : impl_->clients) {
                ws->close();
            }
            impl_->clients.clear();
            impl_->client_count.store(0, std::memory_order_relaxed);

            // Close the listen socket, which causes app->run() to return
            if (impl_->listen_socket) {
                us_listen_socket_close(0, impl_->listen_socket);
                impl_->listen_socket = nullptr;
            }
        });
    }

    if (impl_->event_thread.joinable()) {
        impl_->event_thread.join();
    }

    std::printf("[WsServer] Shutdown complete\n");
}

size_t WsServer::client_count() const {
    return impl_->client_count.load(std::memory_order_relaxed);
}

} // namespace quantumflow
