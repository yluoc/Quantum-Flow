#include "ws/json_serializer.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace quantumflow {

namespace {

inline void append_u64(std::string& out, uint64_t value) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc()) {
        out.append(buf, ptr);
    } else {
        out += "0";
    }
}

inline void append_u8(std::string& out, uint8_t value) {
    char buf[8];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<uint32_t>(value));
    if (ec == std::errc()) {
        out.append(buf, ptr);
    } else {
        out += "0";
    }
}

inline void append_double(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "0";
        return;
    }

    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%.12g", value);
    if (n <= 0) {
        out += "0";
        return;
    }
    const int len = (n < static_cast<int>(sizeof(buf))) ? n : static_cast<int>(sizeof(buf) - 1);
    out.append(buf, static_cast<size_t>(len));
}

inline void append_json_string(std::string& out, std::string_view value) {
    out.push_back('"');
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char esc[7];
                    std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned int>(
                        static_cast<unsigned char>(c)));
                    out += esc;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

inline void append_level(std::string& out, const PriceLevel& lvl) {
    out += "{\"price\":";
    append_double(out, lvl.price);
    out += ",\"quantity\":";
    append_u64(out, lvl.quantity);
    out += ",\"order_count\":";
    append_u64(out, lvl.order_count);
    out.push_back('}');
}

} // namespace

std::string serialize_book(const BookSnapshot& snapshot) {
    std::string out;
    out.reserve(256 + snapshot.symbol.size() + (snapshot.bids.size() + snapshot.asks.size()) * 64);

    out += "{\"type\":\"book\",\"timestamp_ns\":";
    append_u64(out, snapshot.timestamp_ns);
    out += ",\"data\":{\"symbol\":";
    append_json_string(out, snapshot.symbol);
    out += ",\"best_bid\":";
    append_double(out, snapshot.best_bid);
    out += ",\"best_ask\":";
    append_double(out, snapshot.best_ask);
    out += ",\"mid_price\":";
    append_double(out, snapshot.mid_price);
    out += ",\"bids\":[";

    for (size_t i = 0; i < snapshot.bids.size(); ++i) {
        if (i > 0) out.push_back(',');
        append_level(out, snapshot.bids[i]);
    }

    out += "],\"asks\":[";
    for (size_t i = 0; i < snapshot.asks.size(); ++i) {
        if (i > 0) out.push_back(',');
        append_level(out, snapshot.asks[i]);
    }
    out += "]}}";

    return out;
}

std::string serialize_trades(const std::string& symbol, const std::vector<TradeInfo>& trades, uint64_t timestamp_ns) {
    std::string out;
    out.reserve(256 + symbol.size() * 3 + trades.size() * 96);

    const size_t start = trades.size() > 50 ? trades.size() - 50 : 0;

    out += "{\"type\":\"trades\",\"timestamp_ns\":";
    append_u64(out, timestamp_ns);
    out += ",\"data\":{\"symbol\":";
    append_json_string(out, symbol);
    out += ",\"trades\":[";

    bool first = true;
    for (size_t i = start; i < trades.size(); ++i) {
        const auto& t = trades[i];
        if (!first) out.push_back(',');
        first = false;

        out += "{\"symbol\":";
        append_json_string(out, symbol);
        out += ",\"price\":";
        append_double(out, t.price);
        out += ",\"quantity\":";
        append_u64(out, t.quantity);
        out += ",\"side\":";
        append_u8(out, t.side);
        out += ",\"timestamp_ns\":";
        append_u64(out, t.timestamp_ns);
        out.push_back('}');
    }

    out += "]}}";
    return out;
}

std::string serialize_latency(const LatencySnapshot& latency, uint64_t timestamp_ns) {
    std::string out;
    out.reserve(256);

    out += "{\"type\":\"latency\",\"timestamp_ns\":";
    append_u64(out, timestamp_ns);
    out += ",\"data\":{\"python_to_cpp_us\":";
    append_double(out, latency.python_to_cpp_us);
    out += ",\"order_match_us\":";
    append_double(out, latency.order_match_us);
    out += ",\"strategy_eval_us\":";
    append_double(out, latency.strategy_eval_us);
    out += ",\"ws_broadcast_us\":";
    append_double(out, latency.ws_broadcast_us);
    out += ",\"total_us\":";
    append_double(out, latency.total_us);
    out += "}}";

    return out;
}

std::string serialize_strategies(
    const std::unordered_map<std::string, StrategySignal>& signals,
    uint64_t timestamp_ns)
{
    std::string out;
    out.reserve(256 + signals.size() * 128);

    out += "{\"type\":\"strategies\",\"timestamp_ns\":";
    append_u64(out, timestamp_ns);
    out += ",\"data\":{\"signals\":[";

    bool first = true;
    for (const auto& [name, sig] : signals) {
        (void)name;
        if (!first) out.push_back(',');
        first = false;

        out += "{\"strategy_name\":";
        append_json_string(out, sig.strategy_name);
        out += ",\"symbol\":";
        append_json_string(out, sig.symbol);
        out += ",\"signal\":";
        append_json_string(out, signal_to_string(sig.signal));
        out += ",\"confidence\":";
        append_double(out, sig.confidence);
        out += ",\"timestamp_ns\":";
        append_u64(out, sig.timestamp_ns);
        out.push_back('}');
    }

    out += "]}}";
    return out;
}

} // namespace quantumflow
