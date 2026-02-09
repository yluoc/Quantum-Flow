#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

#include "LOB/Book.h"
#include "common/price_converter.hpp"
#include "common/market_data_packet.hpp"
#include "bridge/shared_memory.hpp"
#include "strategies/strategy_base.hpp"
#include "strategies/strategy_engine.hpp"
#include "strategies/microstructure/order_book_imbalance.hpp"
#include "strategies/microstructure/market_maker.hpp"
#include "strategies/microstructure/vwap_executor.hpp"
#include "strategies/microstructure/liquidity_detector.hpp"
#include "strategies/crypto/funding_arbitrage.hpp"
#include "strategies/crypto/momentum.hpp"
#include "strategies/equities/pairs_trading.hpp"

#ifndef QUANTUMFLOW_HEADLESS
#include "ws/ws_server.hpp"
#include "ws/json_serializer.hpp"
#include "common/latency_snapshot.hpp"
#endif

using Clock = std::chrono::steady_clock;

static uint64_t now_ns() {
    return static_cast<uint64_t>(Clock::now().time_since_epoch().count());
}

static double ns_to_us(uint64_t ns) {
    return static_cast<double>(ns) / 1000.0;
}

struct Config {
    std::vector<std::string> symbols;
    bool headless = false;
    int ws_port = 9001;
};

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    cfg.symbols = {"BTC-USDT-SWAP", "ETH-USDT-SWAP"};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
        } else if (std::strcmp(argv[i], "--symbols") == 0 && i + 1 < argc) {
            cfg.symbols.clear();
            std::istringstream ss(argv[++i]);
            std::string token;
            while (std::getline(ss, token, ',')) {
                if (!token.empty()) cfg.symbols.push_back(token);
            }
        } else if (std::strcmp(argv[i], "--ws-port") == 0 && i + 1 < argc) {
            cfg.ws_port = std::atoi(argv[++i]);
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

#ifdef QUANTUMFLOW_HEADLESS
    cfg.headless = true;
#endif

    std::printf("QuantumFlow Trading Engine\n");
    std::printf("Symbols:");
    for (const auto& s : cfg.symbols) std::printf(" %s", s.c_str());
    std::printf("\nMode: %s\n", cfg.headless ? "headless" : "WebUI");

    // --- Price converter registry ---
    quantumflow::PriceConverterRegistry price_reg(100.0);

    // --- Per-symbol order books ---
    std::unordered_map<std::string, std::unique_ptr<Book>> books;
    for (const auto& sym : cfg.symbols) {
        books[sym] = std::make_unique<Book>();
    }

    // --- Strategy engine ---
    quantumflow::StrategyEngine strategy_engine;
    strategy_engine.add_strategy(std::make_unique<quantumflow::OrderBookImbalance>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::MarketMaker>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::VWAPExecutor>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::LiquidityDetector>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::FundingArbitrage>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::MomentumStrategy>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::PairsTrading>());

    // --- Shared memory bridge (global, shared with PyBind11 module) ---
    auto& bridge = quantumflow::global_bridge();

    // --- Recent trades buffer per symbol ---
    std::unordered_map<std::string, std::vector<quantumflow::TradeInfo>> recent_trades;
    for (const auto& sym : cfg.symbols)
        recent_trades[sym] = {};

    // --- Order ID counter (for incoming market data that needs IDs) ---
    uint64_t next_order_id = 1;

    // --- WebSocket server setup ---
#ifndef QUANTUMFLOW_HEADLESS
    quantumflow::WsServer ws_server;
    std::vector<quantumflow::TradeInfo> ws_trade_buffer;
    uint64_t last_broadcast_ns = 0;
    constexpr uint64_t BROADCAST_INTERVAL_NS = 33'333'333; // ~30 Hz

    if (!cfg.headless) {
        if (!ws_server.init(cfg.ws_port)) {
            std::fprintf(stderr, "Failed to init WebSocket server, falling back to headless\n");
            cfg.headless = true;
        }
    }
#endif

    std::printf("Entering main loop. Waiting for market data on ring buffer...\n");

    uint64_t loop_count = 0;
    bool running = true;

    while (running) {
        uint64_t loop_start = now_ns();

        // --- Drain ring buffer ---
        quantumflow::MarketDataPacket pkt;
        int drained = 0;
        constexpr int MAX_DRAIN_PER_FRAME = 256;

        while (drained < MAX_DRAIN_PER_FRAME && bridge.pop(pkt)) {
            std::string sym(pkt.symbol);
            auto it = books.find(sym);
            if (it == books.end()) {
                // Create book on-the-fly for unknown symbols
                books[sym] = std::make_unique<Book>();
                recent_trades[sym] = {};
                it = books.find(sym);
            }

            const auto& converter = price_reg.get(sym);

            if (pkt.event_type == 0) {
                // Book level update: place as a limit order
                OrderType ot = (pkt.side == 0) ? BUY : SELL;
                PRICE internal_price = converter.to_internal(pkt.price);
                const Trades& trades = it->second->place_order(
                    next_order_id++, 0, ot, internal_price, pkt.quantity);

                // Process fills immediately
                for (const auto& t : trades) {
                    quantumflow::TradeInfo ti{
                        converter.to_external(t.get_trade_price()),
                        t.get_trade_volume(),
                        pkt.side,
                        pkt.timestamp_ns
                    };
                    recent_trades[sym].push_back(ti);
                    strategy_engine.on_trade(ti);
#ifndef QUANTUMFLOW_HEADLESS
                    if (!cfg.headless) ws_trade_buffer.push_back(ti);
#endif
                }
            } else if (pkt.event_type == 1) {
                // Trade event
                quantumflow::TradeInfo ti{pkt.price, pkt.quantity, pkt.side, pkt.timestamp_ns};
                recent_trades[sym].push_back(ti);
                strategy_engine.on_trade(ti);
#ifndef QUANTUMFLOW_HEADLESS
                if (!cfg.headless) ws_trade_buffer.push_back(ti);
#endif
            }

            drained++;
        }

        // --- Run strategies on first symbol's book ---
        uint64_t strat_start = now_ns();
        quantumflow::BookSnapshot snapshot;
        if (!cfg.symbols.empty()) {
            const auto& primary_sym = cfg.symbols[0];
            auto bit = books.find(primary_sym);
            if (bit != books.end()) {
                snapshot = quantumflow::BookSnapshot::from_book(
                    *bit->second, primary_sym, price_reg.get(primary_sym));
                snapshot.timestamp_ns = now_ns();

                // Cap recent trades buffer
                auto& trades_buf = recent_trades[primary_sym];
                if (trades_buf.size() > 1000) {
                    trades_buf.erase(trades_buf.begin(),
                                     trades_buf.begin() +
                                         static_cast<long>(trades_buf.size() - 500));
                }

                strategy_engine.evaluate(snapshot, trades_buf);
            }
        }
        uint64_t strat_end = now_ns();

        // --- WebSocket broadcast at 30 Hz ---
#ifndef QUANTUMFLOW_HEADLESS
        if (!cfg.headless) {
            uint64_t now = now_ns();
            if (now - last_broadcast_ns >= BROADCAST_INTERVAL_NS) {
                uint64_t broadcast_start = now_ns();

                // Serialize and broadcast all data types
                if (!snapshot.symbol.empty()) {
                    ws_server.broadcast(quantumflow::serialize_book(snapshot));
                }

                ws_server.broadcast(
                    quantumflow::serialize_trades(ws_trade_buffer, now));

                ws_server.broadcast(
                    quantumflow::serialize_strategies(
                        strategy_engine.all_signals(), now));

                // Latency snapshot
                uint64_t broadcast_end = now_ns();
                quantumflow::LatencySnapshot lat{};
                lat.python_to_cpp_us = 0; // would need packet timestamps
                lat.order_match_us = ns_to_us(strat_start - loop_start);
                lat.strategy_eval_us = ns_to_us(strat_end - strat_start);
                lat.ws_broadcast_us = ns_to_us(broadcast_end - broadcast_start);
                lat.total_us = ns_to_us(broadcast_end - loop_start);

                ws_server.broadcast(
                    quantumflow::serialize_latency(lat, now));

                // Cap trade buffer for WS (keep last 200)
                if (ws_trade_buffer.size() > 200) {
                    ws_trade_buffer.erase(
                        ws_trade_buffer.begin(),
                        ws_trade_buffer.begin() +
                            static_cast<long>(ws_trade_buffer.size() - 200));
                }

                last_broadcast_ns = now;
            }

            // Poll WebSocket I/O (non-blocking)
            ws_server.poll();
        }
#endif

        // --- Headless console output ---
        if (cfg.headless) {
            loop_count++;
            if (loop_count % 1000 == 0) {
                std::printf("[loop %lu] bridge: pushed=%lu popped=%lu dropped=%lu | "
                            "drained=%d | strategies=%zu\n",
                            loop_count,
                            bridge.push_count(), bridge.pop_count(), bridge.drop_count(),
                            drained, strategy_engine.strategy_count());
            }
            // Small sleep in headless to avoid busy-spinning when no data
            if (drained == 0) {
                struct timespec ts = {0, 100000}; // 100us
                nanosleep(&ts, nullptr);
            }
        }
    }

#ifndef QUANTUMFLOW_HEADLESS
    if (!cfg.headless) {
        ws_server.shutdown();
    }
#endif

    std::printf("QuantumFlow shutdown. Bridge stats: pushed=%lu popped=%lu dropped=%lu\n",
                bridge.push_count(), bridge.pop_count(), bridge.drop_count());
    return 0;
}
