// Normalized market data event model (mirrors pipeline/src/normalizer.py structs).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace quantumflow::pipeline {

/// Single order book level: (price, size, count).
struct BookLevel {
    double price = 0.0;
    double size = 0.0;
    int64_t count = 0;
};

/// Payload for book_topn events.
struct BookPayload {
    int n = 0;
    double best_bid = 0.0;
    double best_ask = 0.0;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

/// Payload for trade events.
struct TradePayload {
    double price = 0.0;
    double size = 0.0;
    std::string side;
    std::optional<std::string> trade_id;
};

/// Normalized market data event.
struct NormalizedEvent {
    std::string exchange;
    std::string symbol;
    std::string channel;
    std::string event_type;
    int64_t ts_exchange_ms = 0;
    int64_t ts_recv_epoch_ms = 0;
    int64_t ts_recv_mono_ns = 0;
    int64_t ts_decoded_mono_ns = 0;
    int64_t ts_proc_mono_ns = 0;
    std::variant<BookPayload, TradePayload> payload;

    bool is_book() const { return std::holds_alternative<BookPayload>(payload); }
    bool is_trade() const { return std::holds_alternative<TradePayload>(payload); }
    const BookPayload& book() const { return std::get<BookPayload>(payload); }
    const TradePayload& trade() const { return std::get<TradePayload>(payload); }
};

} // namespace quantumflow::pipeline
