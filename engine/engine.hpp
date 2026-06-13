#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LOB/Book.h"
#include "bridge/shared_memory.hpp"
#include "common/price_converter.hpp"
#include "strategies/strategy_base.hpp"
#include "strategies/strategy_engine.hpp"

#ifndef QUANTUMFLOW_HEADLESS
#include "ws/ws_server.hpp"
#endif

namespace quantumflow {

/// Runtime configuration for the trading engine, populated from CLI args.
struct Config {
    std::vector<std::string> symbols;
    bool headless = false;
    int ws_port = 9001;
    std::string bridge_socket_path = "/tmp/quantumflow_bridge.sock";
    std::string pipeline_control_socket_path = "/tmp/quantumflow_pipeline_ctrl.sock";
};

/// Parse command-line arguments into a Config (defaults applied for missing flags).
Config parse_args(int argc, char* argv[]);

/// QuantumFlow trading engine: owns the order books, strategy engine, market-data
/// ingress (lock-free bridge + Unix datagram socket) and, in WebUI builds, the
/// WebSocket broadcast. Drives the per-frame tick loop in run().
class Engine {
public:
    explicit Engine(Config cfg);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Set up books, strategies and ingress sockets. Returns false on fatal error.
    /// In WebUI builds, a failed WebSocket init degrades to headless rather than failing.
    bool init();

    /// Run the main tick loop until stop() is called.
    void run();

    /// Request the run loop to exit after the current frame.
    void stop() { running_ = false; }

private:
    /// Look up the book for a symbol, lazily creating book/trade buffers if unseen.
    Book& ensure_symbol(const std::string& sym);

    /// Apply a single market-data packet to its book and strategies.
    void process_packet(const MarketDataPacket& pkt);

    /// Drain pending packets from the lock-free bridge, up to `budget`. Returns count.
    int drain_bridge(int budget);

    /// Drain pending packets from the Unix datagram socket, up to `budget`. Returns count.
    int drain_socket(int budget);

    /// Build a snapshot of the active symbol, trim its trade buffer, and run strategies.
    /// Writes the strategy-eval start/end timestamps for latency reporting.
    void evaluate_active(uint64_t& strat_start, uint64_t& strat_end);

    /// Print periodic bridge/ingress stats and back off when idle (headless only).
    void headless_tick(int drained);

    Config cfg_;
    PriceConverterRegistry price_reg_;
    std::unordered_map<std::string, std::unique_ptr<Book>> books_;
    StrategyEngine strategy_engine_;
    MarketDataBridge& bridge_;

    int bridge_socket_fd_ = -1;
    uint64_t bridge_socket_rx_ = 0;
    uint64_t bridge_socket_bad_ = 0;

    std::unordered_map<std::string, std::vector<TradeInfo>> recent_trades_;
    uint64_t next_order_id_ = 1;
    std::string active_symbol_;
    double latest_python_to_cpp_us_ = 0.0;
    uint64_t loop_count_ = 0;
    bool running_ = true;

#ifndef QUANTUMFLOW_HEADLESS
    /// Broadcast all book snapshots, trades, strategy signals and latency to clients.
    void broadcast_frame(uint64_t loop_start, uint64_t strat_start, uint64_t strat_end);

    WsServer ws_server_;
    std::unordered_map<std::string, std::vector<TradeInfo>> ws_trade_buffers_;
    uint64_t last_broadcast_ns_ = 0;
#endif
};

} // namespace quantumflow
