#include "okx_ws.hpp"

#include <cstdio>

#include <ixwebsocket/IXWebSocket.h>

#include "time_helpers.hpp"

namespace quantumflow::pipeline {

OkxWsClient::OkxWsClient(std::string url,
                         std::vector<std::string> channels,
                         FrameCallback on_frame)
    : url_(std::move(url)),
      channels_(std::move(channels)),
      on_frame_(std::move(on_frame)),
      ws_(std::make_unique<ix::WebSocket>()) {

    ws_->setUrl(url_);
    // Protocol-level ping keep-alive (Python used websockets ping_interval=20).
    ws_->setPingInterval(20);
    // Built-in exponential backoff reconnection (replaces the manual backoff loop).
    ws_->enableAutomaticReconnection();
    ws_->setMinWaitBetweenReconnectionRetries(250);   // ms
    ws_->setMaxWaitBetweenReconnectionRetries(30000);  // ms

    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Message: {
                // Capture receive timestamps IMMEDIATELY (first lines after arrival).
                const int64_t ts_recv_epoch_ms = now_epoch_ms();
                const int64_t ts_recv_mono_ns = now_mono_ns();

                nlohmann::json parsed;
                try {
                    parsed = nlohmann::json::parse(msg->str);
                } catch (const nlohmann::json::parse_error&) {
                    return;  // Skip invalid JSON (mirrors msgspec.DecodeError).
                }
                if (!parsed.is_object()) return;

                const int64_t ts_decoded_mono_ns = now_mono_ns();
                if (on_frame_) {
                    on_frame_(ts_recv_epoch_ms, ts_recv_mono_ns, ts_decoded_mono_ns, parsed);
                }
                break;
            }
            case ix::WebSocketMessageType::Open: {
                std::vector<std::string> syms;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    open_ = true;
                    syms = symbols_;
                }
                if (!syms.empty()) send_subscribe(syms);
                break;
            }
            case ix::WebSocketMessageType::Close: {
                std::lock_guard<std::mutex> lk(mu_);
                open_ = false;
                break;
            }
            case ix::WebSocketMessageType::Error: {
                std::fprintf(stderr, "[okx_ws] error: %s\n", msg->errorInfo.reason.c_str());
                break;
            }
            default:
                break;
        }
    });
}

OkxWsClient::~OkxWsClient() {
    stop();
}

std::string OkxWsClient::build_sub_text(const std::vector<std::string>& symbols,
                                        const char* op) const {
    nlohmann::json args = nlohmann::json::array();
    for (const auto& sym : symbols) {
        for (const auto& ch : channels_) {
            args.push_back({{"channel", ch}, {"instId", sym}});
        }
    }
    nlohmann::json payload = {{"op", op}, {"args", std::move(args)}};
    return payload.dump();
}

void OkxWsClient::send_subscribe(const std::vector<std::string>& symbols) {
    if (symbols.empty()) return;
    ws_->send(build_sub_text(symbols, "subscribe"));
}

void OkxWsClient::send_unsubscribe(const std::vector<std::string>& symbols) {
    if (symbols.empty()) return;
    ws_->send(build_sub_text(symbols, "unsubscribe"));
}

void OkxWsClient::set_symbols(std::vector<std::string> symbols) {
    std::vector<std::string> old_syms;
    bool is_open;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (symbols == symbols_) return;
        old_syms = symbols_;
        symbols_ = symbols;
        is_open = open_;
    }
    if (is_open) {
        send_unsubscribe(old_syms);
        send_subscribe(symbols);
    }
}

void OkxWsClient::start() {
    ws_->start();
}

void OkxWsClient::stop() {
    if (ws_) {
        ws_->stop();
    }
}

} // namespace quantumflow::pipeline
