#include "engine/engine.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <ctime>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/unix_socket.hpp"
#include "strategies/microstructure/order_book_imbalance.hpp"
#include "strategies/microstructure/market_maker.hpp"
#include "strategies/microstructure/vwap_executor.hpp"
#include "strategies/microstructure/liquidity_detector.hpp"
#include "strategies/crypto/funding_arbitrage.hpp"
#include "strategies/crypto/momentum.hpp"
#include "strategies/equities/pairs_trading.hpp"

#ifndef QUANTUMFLOW_HEADLESS
#include "ws/json_serializer.hpp"
#include "common/latency_snapshot.hpp"
#include <nlohmann/json.hpp>
#endif

namespace quantumflow {

namespace {

using Clock = std::chrono::steady_clock;

uint64_t now_ns() {
    return static_cast<uint64_t>(Clock::now().time_since_epoch().count());
}

double ns_to_us(uint64_t ns) {
    return static_cast<double>(ns) / 1000.0;
}

constexpr int MAX_DRAIN_PER_FRAME = 256;

int open_bridge_socket(const std::string& path) {
    if (!unix_path_fits(path)) {
        std::fprintf(stderr, "Bridge socket path too long: %s\n", path.c_str());
        return -1;
    }

    int fd = make_unix_dgram_socket();
    if (fd < 0) {
        std::fprintf(stderr, "Failed to create bridge socket: %s\n", std::strerror(errno));
        return -1;
    }

    set_nonblocking(fd);

    if (!bind_unix_dgram(fd, path)) {
        std::fprintf(stderr, "Failed to bind bridge socket %s: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return -1;
    }

    return fd;
}

#ifndef QUANTUMFLOW_HEADLESS
constexpr uint64_t BROADCAST_INTERVAL_NS = 33'333'333; // ~30 Hz

bool send_pipeline_symbol_update(
    const std::string& control_socket_path,
    const std::vector<std::string>& symbols) {
    if (symbols.empty()) return false;
    if (!unix_path_fits(control_socket_path)) {
        std::fprintf(stderr, "Pipeline control socket path too long: %s\n",
                     control_socket_path.c_str());
        return false;
    }

    nlohmann::json msg = {
        {"type", "set_symbols"},
        {"symbols", symbols}
    };
    const std::string payload = msg.dump();

    ssize_t sent = send_unix_dgram(control_socket_path, payload.data(), payload.size());
    if (sent < 0) {
        std::fprintf(stderr, "Failed to send symbols to pipeline control socket %s: %s\n",
                     control_socket_path.c_str(), std::strerror(errno));
        return false;
    }
    return true;
}
#endif

} // namespace

Config parse_args(int argc, char* argv[]) {
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
        } else if (std::strcmp(argv[i], "--pipeline-control-socket") == 0 && i + 1 < argc) {
            cfg.pipeline_control_socket_path = argv[++i];
        }
    }
    return cfg;
}

Engine::Engine(Config cfg)
    : cfg_(std::move(cfg)),
      price_reg_(100.0),
      bridge_(global_bridge()) {
#ifdef QUANTUMFLOW_HEADLESS
    cfg_.headless = true;
#endif
    active_symbol_ = cfg_.symbols.empty() ? std::string() : cfg_.symbols[0];
}

Engine::~Engine() {
#ifndef QUANTUMFLOW_HEADLESS
    if (!cfg_.headless) {
        ws_server_.shutdown();
    }
#endif

    if (bridge_socket_fd_ >= 0) {
        ::close(bridge_socket_fd_);
        (void)::unlink(cfg_.bridge_socket_path.c_str());
    }

    std::printf("QuantumFlow shutdown. Bridge stats: pushed=%lu popped=%lu dropped=%lu | "
                "uds_rx=%lu uds_bad=%lu\n",
                bridge_.push_count(), bridge_.pop_count(), bridge_.drop_count(),
                bridge_socket_rx_, bridge_socket_bad_);
}

bool Engine::init() {
    std::printf("QuantumFlow Trading Engine\n");
    std::printf("Symbols:");
    for (const auto& s : cfg_.symbols) std::printf(" %s", s.c_str());
    std::printf("\nMode: %s\n", cfg_.headless ? "headless" : "WebUI");
    std::printf("Bridge Socket: %s\n", cfg_.bridge_socket_path.c_str());
    std::printf("Pipeline Control Socket: %s\n", cfg_.pipeline_control_socket_path.c_str());

    for (const auto& sym : cfg_.symbols) {
        books_[sym] = std::make_unique<lob::Book>();
        recent_trades_[sym] = {};
#ifndef QUANTUMFLOW_HEADLESS
        ws_trade_buffers_[sym] = {};
#endif
    }

    strategy_engine_.add_strategy(std::make_unique<OrderBookImbalance>());
    strategy_engine_.add_strategy(std::make_unique<MarketMaker>());
    strategy_engine_.add_strategy(std::make_unique<VWAPExecutor>());
    strategy_engine_.add_strategy(std::make_unique<LiquidityDetector>());
    strategy_engine_.add_strategy(std::make_unique<FundingArbitrage>());
    strategy_engine_.add_strategy(std::make_unique<MomentumStrategy>());
    strategy_engine_.add_strategy(std::make_unique<PairsTrading>());

    bridge_socket_fd_ = open_bridge_socket(cfg_.bridge_socket_path);

#ifndef QUANTUMFLOW_HEADLESS
    if (!cfg_.headless) {
        ws_server_.set_message_handler([this](const std::string& raw_msg) {
            try {
                auto msg = nlohmann::json::parse(raw_msg);
                if (!msg.is_object() || msg.value("type", "") != "set_symbols") {
                    return;
                }
                const auto symbols_json = msg.find("symbols");
                if (symbols_json == msg.end() || !symbols_json->is_array()) {
                    return;
                }
                std::vector<std::string> symbols;
                symbols.reserve(symbols_json->size());
                for (const auto& item : *symbols_json) {
                    if (!item.is_string()) continue;
                    std::string symbol = item.get<std::string>();
                    if (!symbol.empty()) symbols.push_back(std::move(symbol));
                }
                if (symbols.empty()) {
                    return;
                }
                (void)send_pipeline_symbol_update(cfg_.pipeline_control_socket_path, symbols);
            } catch (...) {
            }
        });
        if (!ws_server_.init(cfg_.ws_port)) {
            std::fprintf(stderr, "Failed to init WebSocket server, falling back to headless\n");
            cfg_.headless = true;
        }
    }
#endif

    std::printf("Entering main loop. Waiting for market data on bridge ingress...\n");
    return true;
}

lob::Book& Engine::ensure_symbol(const std::string& sym) {
    auto it = books_.find(sym);
    if (it == books_.end()) {
        books_[sym] = std::make_unique<lob::Book>();
        recent_trades_[sym] = {};
#ifndef QUANTUMFLOW_HEADLESS
        ws_trade_buffers_[sym] = {};
#endif
        it = books_.find(sym);
    }
    return *it->second;
}

void Engine::refresh_symbol_cache(const std::string& sym) {
    cache_.symbol = sym;
    cache_.book = &ensure_symbol(sym);
    cache_.converter = &price_reg_.get(sym);
    cache_.recent = &recent_trades_[sym];
#ifndef QUANTUMFLOW_HEADLESS
    cache_.ws = &ws_trade_buffers_[sym];
#endif
}

void Engine::process_packet(const MarketDataPacket& pkt) {
    char symbol_buf[sizeof(pkt.symbol) + 1]{};
    std::memcpy(symbol_buf, pkt.symbol, sizeof(pkt.symbol));
    std::string sym(symbol_buf);
    if (sym.empty()) {
        return;
    }

    active_symbol_ = sym;

    // Packets arrive in per-symbol bursts; only re-resolve the book/converter/
    // buffers when the symbol actually changes from the previous packet.
    if (sym != cache_.symbol) {
        refresh_symbol_cache(sym);
    }

    lob::Book& book = *cache_.book;
    const PriceConverter& converter = *cache_.converter;

    uint64_t ingest_ns = now_ns();
    if (pkt.timestamp_ns > 0 && ingest_ns >= pkt.timestamp_ns) {
        latest_python_to_cpp_us_ = ns_to_us(ingest_ns - pkt.timestamp_ns);
    }

    if (pkt.event_type == 0) {
        lob::OrderType ot = (pkt.side == 0) ? lob::BUY : lob::SELL;
        lob::PRICE internal_price = converter.to_internal(pkt.price);
        const lob::Trades& trades = book.place_order(
            next_order_id_++, 0, ot, internal_price, pkt.quantity);

        for (const auto& t : trades) {
            TradeInfo ti{
                converter.to_external(t.get_trade_price()),
                t.get_trade_volume(),
                pkt.side,
                pkt.timestamp_ns
            };
            cache_.recent->push(ti);
            strategy_engine_.on_trade(ti);
#ifndef QUANTUMFLOW_HEADLESS
            if (!cfg_.headless) cache_.ws->push(ti);
#endif
        }
    } else if (pkt.event_type == 1) {
        TradeInfo ti{pkt.price, pkt.quantity, pkt.side, pkt.timestamp_ns};
        cache_.recent->push(ti);
        strategy_engine_.on_trade(ti);
#ifndef QUANTUMFLOW_HEADLESS
        if (!cfg_.headless) cache_.ws->push(ti);
#endif
    }
}

int Engine::drain_bridge(int budget) {
    int drained = 0;
    MarketDataPacket pkt{};
    while (drained < budget && bridge_.pop(pkt)) {
        process_packet(pkt);
        drained++;
    }
    return drained;
}

int Engine::drain_socket(int budget) {
    if (bridge_socket_fd_ < 0) {
        return 0;
    }
    int drained = 0;
    while (drained < budget) {
        MarketDataPacket sock_pkt{};
        ssize_t n = ::recv(bridge_socket_fd_, &sock_pkt, sizeof(sock_pkt), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            std::fprintf(stderr, "Bridge socket recv error: %s\n", std::strerror(errno));
            break;
        }
        if (static_cast<size_t>(n) != sizeof(sock_pkt)) {
            bridge_socket_bad_++;
            continue;
        }
        process_packet(sock_pkt);
        bridge_socket_rx_++;
        drained++;
    }
    return drained;
}

void Engine::evaluate_active(uint64_t& strat_start, uint64_t& strat_end) {
    strat_start = now_ns();
    if (!active_symbol_.empty()) {
        const auto& primary_sym = active_symbol_;
        auto bit = books_.find(primary_sym);
        if (bit != books_.end()) {
            BookSnapshot snapshot = BookSnapshot::from_book(
                *bit->second, primary_sym, price_reg_.get(primary_sym));
            snapshot.timestamp_ns = now_ns();

            auto& trades_buf = recent_trades_[primary_sym];
            trades_buf.trim();

            strategy_engine_.evaluate(snapshot, trades_buf.view());
        }
    }
    strat_end = now_ns();
}

#ifndef QUANTUMFLOW_HEADLESS
void Engine::broadcast_frame(uint64_t loop_start, uint64_t strat_start, uint64_t strat_end) {
    uint64_t now = now_ns();
    if (now - last_broadcast_ns_ < BROADCAST_INTERVAL_NS) {
        return;
    }
    uint64_t broadcast_start = now_ns();

    for (const auto& [sym, book_ptr] : books_) {
        BookSnapshot ws_snapshot = BookSnapshot::from_book(
            *book_ptr, sym, price_reg_.get(sym));
        ws_snapshot.timestamp_ns = now;
        ws_server_.broadcast(serialize_book(ws_snapshot));
    }

    for (auto& [sym, trades] : ws_trade_buffers_) {
        ws_server_.broadcast(serialize_trades(sym, trades.view(), now));
        trades.trim();
    }

    ws_server_.broadcast(serialize_strategies(strategy_engine_.all_signals(), now));

    uint64_t broadcast_end = now_ns();
    LatencySnapshot lat{};
    lat.python_to_cpp_us = latest_python_to_cpp_us_;
    lat.order_match_us = ns_to_us(strat_start - loop_start);
    lat.strategy_eval_us = ns_to_us(strat_end - strat_start);
    lat.ws_broadcast_us = ns_to_us(broadcast_end - broadcast_start);
    lat.total_us = ns_to_us(broadcast_end - loop_start);

    ws_server_.broadcast(serialize_latency(lat, now));

    last_broadcast_ns_ = now;
}
#endif

void Engine::headless_tick(int drained) {
    loop_count_++;
    if (loop_count_ % 1000 == 0) {
        std::printf("[loop %lu] bridge: pushed=%lu popped=%lu dropped=%lu | "
                    "uds_rx=%lu uds_bad=%lu | drained=%d | strategies=%zu\n",
                    loop_count_,
                    bridge_.push_count(), bridge_.pop_count(), bridge_.drop_count(),
                    bridge_socket_rx_, bridge_socket_bad_,
                    drained, strategy_engine_.strategy_count());
    }
    // Small sleep in headless to avoid busy-spinning when no data
    if (drained == 0) {
        struct timespec ts = {0, 100000}; // 100us
        nanosleep(&ts, nullptr);
    }
}

void Engine::run() {
    while (running_) {
        uint64_t loop_start = now_ns();

        int drained = drain_bridge(MAX_DRAIN_PER_FRAME);
        drained += drain_socket(MAX_DRAIN_PER_FRAME - drained);

        uint64_t strat_start = 0;
        uint64_t strat_end = 0;
        evaluate_active(strat_start, strat_end);

#ifndef QUANTUMFLOW_HEADLESS
        if (!cfg_.headless) {
            broadcast_frame(loop_start, strat_start, strat_end);
            ws_server_.poll();
        }
#else
        (void)loop_start;
#endif

        if (cfg_.headless) {
            headless_tick(drained);
        }
    }
}

} // namespace quantumflow
