#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <ctime>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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
    std::string bridge_socket_path = "/tmp/quantumflow_bridge.sock";
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
        } else if (std::strcmp(argv[i], "--bridge-socket") == 0 && i + 1 < argc) {
            cfg.bridge_socket_path = argv[++i];
        }
    }
    return cfg;
}

static int open_bridge_socket(const std::string& path) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "Bridge socket path too long: %s\n", path.c_str());
        return -1;
    }

    int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "Failed to create bridge socket: %s\n", std::strerror(errno));
        return -1;
    }

    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    (void)::unlink(path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "Failed to bind bridge socket %s: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return -1;
    }

    return fd;
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
    std::printf("Bridge Socket: %s\n", cfg.bridge_socket_path.c_str());

    quantumflow::PriceConverterRegistry price_reg(100.0);

    std::unordered_map<std::string, std::unique_ptr<Book>> books;
    for (const auto& sym : cfg.symbols) {
        books[sym] = std::make_unique<Book>();
    }

    quantumflow::StrategyEngine strategy_engine;
    strategy_engine.add_strategy(std::make_unique<quantumflow::OrderBookImbalance>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::MarketMaker>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::VWAPExecutor>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::LiquidityDetector>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::FundingArbitrage>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::MomentumStrategy>());
    strategy_engine.add_strategy(std::make_unique<quantumflow::PairsTrading>());

    auto& bridge = quantumflow::global_bridge();
    int bridge_socket_fd = open_bridge_socket(cfg.bridge_socket_path);
    uint64_t bridge_socket_rx = 0;
    uint64_t bridge_socket_bad = 0;

    std::unordered_map<std::string, std::vector<quantumflow::TradeInfo>> recent_trades;
    for (const auto& sym : cfg.symbols)
        recent_trades[sym] = {};

    uint64_t next_order_id = 1;

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

    std::printf("Entering main loop. Waiting for market data on bridge ingress...\n");

    uint64_t loop_count = 0;
    bool running = true;
    std::string active_symbol = cfg.symbols.empty() ? "" : cfg.symbols[0];
    double latest_python_to_cpp_us = 0.0;

    while (running) {
        uint64_t loop_start = now_ns();

        int drained = 0;
        constexpr int MAX_DRAIN_PER_FRAME = 256;

        auto process_packet = [&](const quantumflow::MarketDataPacket& pkt) {
            char symbol_buf[sizeof(pkt.symbol) + 1]{};
            std::memcpy(symbol_buf, pkt.symbol, sizeof(pkt.symbol));
            std::string sym(symbol_buf);
            if (sym.empty()) {
                return;
            }

            active_symbol = sym;

            auto it = books.find(sym);
            if (it == books.end()) {
                books[sym] = std::make_unique<Book>();
                recent_trades[sym] = {};
                it = books.find(sym);
            }

            uint64_t ingest_ns = now_ns();
            if (pkt.timestamp_ns > 0 && ingest_ns >= pkt.timestamp_ns) {
                latest_python_to_cpp_us = ns_to_us(ingest_ns - pkt.timestamp_ns);
            }

            const auto& converter = price_reg.get(sym);

            if (pkt.event_type == 0) {
                OrderType ot = (pkt.side == 0) ? BUY : SELL;
                PRICE internal_price = converter.to_internal(pkt.price);
                const Trades& trades = it->second->place_order(
                    next_order_id++, 0, ot, internal_price, pkt.quantity);

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
                quantumflow::TradeInfo ti{pkt.price, pkt.quantity, pkt.side, pkt.timestamp_ns};
                recent_trades[sym].push_back(ti);
                strategy_engine.on_trade(ti);
#ifndef QUANTUMFLOW_HEADLESS
                if (!cfg.headless) ws_trade_buffer.push_back(ti);
#endif
            }
        };

        quantumflow::MarketDataPacket pkt{};
        while (drained < MAX_DRAIN_PER_FRAME && bridge.pop(pkt)) {
            process_packet(pkt);
            drained++;
        }

        if (bridge_socket_fd >= 0) {
            while (drained < MAX_DRAIN_PER_FRAME) {
                quantumflow::MarketDataPacket sock_pkt{};
                ssize_t n = ::recv(bridge_socket_fd, &sock_pkt, sizeof(sock_pkt), 0);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        break;
                    }
                    std::fprintf(stderr, "Bridge socket recv error: %s\n", std::strerror(errno));
                    break;
                }
                if (static_cast<size_t>(n) != sizeof(sock_pkt)) {
                    bridge_socket_bad++;
                    continue;
                }
                process_packet(sock_pkt);
                bridge_socket_rx++;
                drained++;
            }
        }

        uint64_t strat_start = now_ns();
        quantumflow::BookSnapshot snapshot;
        if (!active_symbol.empty()) {
            const auto& primary_sym = active_symbol;
            auto bit = books.find(primary_sym);
            if (bit != books.end()) {
                snapshot = quantumflow::BookSnapshot::from_book(
                    *bit->second, primary_sym, price_reg.get(primary_sym));
                snapshot.timestamp_ns = now_ns();

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

#ifndef QUANTUMFLOW_HEADLESS
        if (!cfg.headless) {
            uint64_t now = now_ns();
            if (now - last_broadcast_ns >= BROADCAST_INTERVAL_NS) {
                uint64_t broadcast_start = now_ns();

                if (!snapshot.symbol.empty()) {
                    ws_server.broadcast(quantumflow::serialize_book(snapshot));
                }

                ws_server.broadcast(
                    quantumflow::serialize_trades(ws_trade_buffer, now));

                ws_server.broadcast(
                    quantumflow::serialize_strategies(
                        strategy_engine.all_signals(), now));

                uint64_t broadcast_end = now_ns();
                quantumflow::LatencySnapshot lat{};
                lat.python_to_cpp_us = latest_python_to_cpp_us;
                lat.order_match_us = ns_to_us(strat_start - loop_start);
                lat.strategy_eval_us = ns_to_us(strat_end - strat_start);
                lat.ws_broadcast_us = ns_to_us(broadcast_end - broadcast_start);
                lat.total_us = ns_to_us(broadcast_end - loop_start);

                ws_server.broadcast(
                    quantumflow::serialize_latency(lat, now));

                if (ws_trade_buffer.size() > 200) {
                    ws_trade_buffer.erase(
                        ws_trade_buffer.begin(),
                        ws_trade_buffer.begin() +
                            static_cast<long>(ws_trade_buffer.size() - 200));
                }

                last_broadcast_ns = now;
            }

            ws_server.poll();
        }
#endif

        if (cfg.headless) {
            loop_count++;
            if (loop_count % 1000 == 0) {
                std::printf("[loop %lu] bridge: pushed=%lu popped=%lu dropped=%lu | "
                            "uds_rx=%lu uds_bad=%lu | drained=%d | strategies=%zu\n",
                            loop_count,
                            bridge.push_count(), bridge.pop_count(), bridge.drop_count(),
                            bridge_socket_rx, bridge_socket_bad,
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

    if (bridge_socket_fd >= 0) {
        ::close(bridge_socket_fd);
        (void)::unlink(cfg.bridge_socket_path.c_str());
    }

    std::printf("QuantumFlow shutdown. Bridge stats: pushed=%lu popped=%lu dropped=%lu | "
                "uds_rx=%lu uds_bad=%lu\n",
                bridge.push_count(), bridge.pop_count(), bridge.drop_count(),
                bridge_socket_rx, bridge_socket_bad);
    return 0;
}
