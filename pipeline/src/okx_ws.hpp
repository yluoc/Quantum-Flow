// OKX WebSocket client - connection, subscription, and raw JSON decoding.
// Mirrors pipeline/src/okx_ws.py. Uses IXWebSocket (TLS + auto-reconnect).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ix { class WebSocket; }

namespace quantumflow::pipeline {

/// Callback invoked for each decoded OKX frame, with the same timestamp triplet
/// captured by the Python generator.
///   ts_recv_epoch_ms  - epoch ms at frame receipt (exchange->recv latency)
///   ts_recv_mono_ns   - monotonic ns at frame receipt (recv->decode latency)
///   ts_decoded_mono_ns- monotonic ns after JSON decode (decode->proc latency)
using FrameCallback = std::function<void(
    int64_t ts_recv_epoch_ms,
    int64_t ts_recv_mono_ns,
    int64_t ts_decoded_mono_ns,
    const nlohmann::json& msg)>;

class OkxWsClient {
public:
    OkxWsClient(std::string url,
                std::vector<std::string> channels,
                FrameCallback on_frame);
    ~OkxWsClient();

    OkxWsClient(const OkxWsClient&) = delete;
    OkxWsClient& operator=(const OkxWsClient&) = delete;

    /// Replace the active symbol set. Thread-safe; if connected, re-subscribes
    /// (unsubscribe old args, subscribe new args).
    void set_symbols(std::vector<std::string> symbols);

    /// Start the background WS thread (non-blocking). Auto-reconnects internally.
    void start();

    /// Stop and join the background WS thread.
    void stop();

private:
    std::string build_sub_text(const std::vector<std::string>& symbols, const char* op) const;
    void send_subscribe(const std::vector<std::string>& symbols);
    void send_unsubscribe(const std::vector<std::string>& symbols);

    std::string url_;
    std::vector<std::string> channels_;
    FrameCallback on_frame_;

    std::unique_ptr<ix::WebSocket> ws_;
    nlohmann::json::parser_callback_t parser_cb_{nullptr};

    mutable std::mutex mu_;
    std::vector<std::string> symbols_;
    bool open_ = false;
};

} // namespace quantumflow::pipeline
