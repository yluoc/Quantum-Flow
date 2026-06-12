#include "normalizer.hpp"

#include "time_helpers.hpp"

namespace quantumflow::pipeline {

namespace {

// OKX sends numeric fields as JSON strings (e.g. "43000.5"). Accept both string
// and number forms; return std::nullopt on failure (mirrors Python's try/except).
std::optional<double> to_double(const nlohmann::json& v) {
    try {
        if (v.is_string()) {
            const std::string& s = v.get_ref<const std::string&>();
            if (s.empty()) return std::nullopt;
            size_t pos = 0;
            double d = std::stod(s, &pos);
            return d;
        }
        if (v.is_number()) {
            return v.get<double>();
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<int64_t> to_int(const nlohmann::json& v) {
    try {
        if (v.is_string()) {
            const std::string& s = v.get_ref<const std::string&>();
            if (s.empty()) return std::nullopt;
            size_t pos = 0;
            // Parse as double then truncate to mirror Python int() on numeric strings,
            // but OKX counts/timestamps are integral strings so stoll is exact.
            long long n = std::stoll(s, &pos);
            return static_cast<int64_t>(n);
        }
        if (v.is_number_integer() || v.is_number_unsigned()) {
            return v.get<int64_t>();
        }
        if (v.is_number_float()) {
            return static_cast<int64_t>(v.get<double>());
        }
    } catch (...) {
    }
    return std::nullopt;
}

// Parse a list of [price, size, _, count] levels. Skips malformed entries.
std::vector<BookLevel> parse_levels(const nlohmann::json& raw_levels) {
    std::vector<BookLevel> levels;
    if (!raw_levels.is_array()) return levels;
    for (const auto& level : raw_levels) {
        if (!level.is_array() || level.size() < 4) continue;
        auto price = to_double(level[0]);
        auto size = to_double(level[1]);
        auto count = to_int(level[3]);
        if (!price || !size || !count) continue;
        levels.push_back(BookLevel{*price, *size, *count});
    }
    return levels;
}

const nlohmann::json* find_member(const nlohmann::json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    auto it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return &(*it);
}

} // namespace

std::vector<NormalizedEvent> normalize_okx(
    int64_t ts_recv_epoch_ms,
    int64_t ts_recv_mono_ns,
    int64_t ts_decoded_mono_ns,
    const nlohmann::json& msg) {

    std::vector<NormalizedEvent> events;
    if (!msg.is_object()) return events;

    // Skip subscription acks / errors.
    if (const auto* ev = find_member(msg, "event"); ev && ev->is_string()) {
        const std::string& e = ev->get_ref<const std::string&>();
        if (e == "subscribe" || e == "unsubscribe" || e == "error") return events;
    }

    const auto* arg = find_member(msg, "arg");
    const auto* data = find_member(msg, "data");
    if (!arg || !data || !data->is_array() || data->empty()) return events;

    const auto* channel_j = find_member(*arg, "channel");
    const auto* inst_j = find_member(*arg, "instId");
    if (!channel_j || !channel_j->is_string()) return events;
    if (!inst_j || !inst_j->is_string()) return events;

    const std::string channel = channel_j->get<std::string>();
    const std::string inst_id = inst_j->get<std::string>();
    if (channel.empty() || inst_id.empty()) return events;

    if (channel == "books5") {
        const auto& d0 = (*data)[0];
        const auto* ts_j = find_member(d0, "ts");
        int64_t ts_exchange_ms = 0;
        if (ts_j) {
            auto ts = to_int(*ts_j);
            if (!ts) return events;
            ts_exchange_ms = *ts;
        }

        BookPayload payload;
        payload.n = 5;
        if (const auto* bids = find_member(d0, "bids")) payload.bids = parse_levels(*bids);
        if (const auto* asks = find_member(d0, "asks")) payload.asks = parse_levels(*asks);
        payload.best_bid = payload.bids.empty() ? 0.0 : payload.bids[0].price;
        payload.best_ask = payload.asks.empty() ? 0.0 : payload.asks[0].price;

        NormalizedEvent ev;
        ev.exchange = "okx";
        ev.symbol = inst_id;
        ev.channel = "books5";
        ev.event_type = "book_topn";
        ev.ts_exchange_ms = ts_exchange_ms;
        ev.ts_recv_epoch_ms = ts_recv_epoch_ms;
        ev.ts_recv_mono_ns = ts_recv_mono_ns;
        ev.ts_decoded_mono_ns = ts_decoded_mono_ns;
        ev.ts_proc_mono_ns = now_mono_ns();
        ev.payload = std::move(payload);
        events.push_back(std::move(ev));

    } else if (channel == "trades") {
        for (const auto& d : *data) {
            const auto* ts_j = find_member(d, "ts");
            if (!ts_j) continue;
            auto ts_exchange_ms = to_int(*ts_j);
            if (!ts_exchange_ms) continue;

            const auto* px = find_member(d, "px");
            const auto* sz = find_member(d, "sz");
            const auto* side = find_member(d, "side");
            if (!px || !sz || !side || !side->is_string()) continue;

            auto price = to_double(*px);
            auto size = to_double(*sz);
            if (!price || !size) continue;

            TradePayload payload;
            payload.price = *price;
            payload.size = *size;
            payload.side = side->get<std::string>();
            if (const auto* tid = find_member(d, "tradeId"); tid && tid->is_string()) {
                payload.trade_id = tid->get<std::string>();
            }

            NormalizedEvent ev;
            ev.exchange = "okx";
            ev.symbol = inst_id;
            ev.channel = "trades";
            ev.event_type = "trade";
            ev.ts_exchange_ms = *ts_exchange_ms;
            ev.ts_recv_epoch_ms = ts_recv_epoch_ms;
            ev.ts_recv_mono_ns = ts_recv_mono_ns;
            ev.ts_decoded_mono_ns = ts_decoded_mono_ns;
            ev.ts_proc_mono_ns = now_mono_ns();
            ev.payload = std::move(payload);
            events.push_back(std::move(ev));
        }
    }

    return events;
}

} // namespace quantumflow::pipeline
