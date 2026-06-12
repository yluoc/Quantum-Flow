// C++ bridge sink - pushes MarketDataPackets to the engine over a Unix datagram
// socket (mirrors the native path of sinks/bridge.py).
#pragma once

#include <cstdint>
#include <string>

#include "sink.hpp"

namespace quantumflow::pipeline {

class CppBridgeSink : public Sink {
public:
    explicit CppBridgeSink(std::string socket_path = "/tmp/quantumflow_bridge.sock");
    ~CppBridgeSink() override;

    void write(const NormalizedEvent& event) override;
    void close() override;

private:
    void send_packet(const std::string& symbol, uint8_t side, uint8_t event_type,
                     double price, uint64_t quantity, uint64_t timestamp_ns,
                     uint64_t order_id = 0);

    std::string socket_path_;
    int fd_ = -1;
    uint64_t sent_ = 0;
    uint64_t dropped_ = 0;
    bool warned_missing_socket_ = false;
    bool closed_ = false;
};

} // namespace quantumflow::pipeline
