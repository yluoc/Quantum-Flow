#include "jsonl_sink.hpp"

#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace quantumflow::pipeline {

namespace {

// data/okx/{channel}/{YYYY-MM-DD}/{symbol}.jsonl using UTC date from epoch ms.
std::string partition_path(const std::string& root, const std::string& channel,
                           const std::string& symbol, int64_t ts_ms) {
    const std::time_t secs = static_cast<std::time_t>(ts_ms / 1000);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &tm_utc);

    namespace fs = std::filesystem;
    return (fs::path(root) / "okx" / channel / date / (symbol + ".jsonl")).string();
}

nlohmann::json event_to_json(const NormalizedEvent& e) {
    nlohmann::json j;
    j["exchange"] = e.exchange;
    j["symbol"] = e.symbol;
    j["channel"] = e.channel;
    j["event_type"] = e.event_type;
    j["ts_exchange_ms"] = e.ts_exchange_ms;
    j["ts_recv_epoch_ms"] = e.ts_recv_epoch_ms;
    j["ts_recv_mono_ns"] = e.ts_recv_mono_ns;
    j["ts_decoded_mono_ns"] = e.ts_decoded_mono_ns;
    j["ts_proc_mono_ns"] = e.ts_proc_mono_ns;

    if (e.is_book()) {
        const BookPayload& p = e.book();
        nlohmann::json bids = nlohmann::json::array();
        for (const auto& lvl : p.bids) bids.push_back({lvl.price, lvl.size, lvl.count});
        nlohmann::json asks = nlohmann::json::array();
        for (const auto& lvl : p.asks) asks.push_back({lvl.price, lvl.size, lvl.count});
        j["payload"] = {
            {"n", p.n},
            {"best_bid", p.best_bid},
            {"best_ask", p.best_ask},
            {"bids", std::move(bids)},
            {"asks", std::move(asks)},
        };
    } else if (e.is_trade()) {
        const TradePayload& p = e.trade();
        nlohmann::json payload = {
            {"price", p.price},
            {"size", p.size},
            {"side", p.side},
        };
        if (p.trade_id) {
            payload["trade_id"] = *p.trade_id;
        } else {
            payload["trade_id"] = nullptr;
        }
        j["payload"] = std::move(payload);
    }
    return j;
}

} // namespace

JsonlSink::JsonlSink(std::string root, double flush_interval_sec, int flush_count)
    : root_(std::move(root)),
      flush_interval_sec_(flush_interval_sec),
      flush_count_(flush_count),
      last_flush_(std::chrono::steady_clock::now()) {}

void JsonlSink::write(const NormalizedEvent& event) {
    const std::string path =
        partition_path(root_, event.channel, event.symbol, event.ts_recv_epoch_ms);

    // Compact serialization (no spaces), matching json.dumps(separators=(",", ":")).
    buffer_[path].push_back(event_to_json(event).dump());
    ++buffer_count_;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_flush_).count();
    if (buffer_count_ >= flush_count_ || elapsed >= flush_interval_sec_) {
        flush();
    }
}

void JsonlSink::flush() {
    if (buffer_.empty()) return;

    namespace fs = std::filesystem;
    for (const auto& [path, lines] : buffer_) {
        if (lines.empty()) continue;
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::app | std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "[jsonl] failed to open %s\n", path.c_str());
            continue;
        }
        for (const auto& line : lines) {
            out << line << '\n';
        }
    }

    buffer_.clear();
    buffer_count_ = 0;
    last_flush_ = std::chrono::steady_clock::now();
}

void JsonlSink::close() {
    flush();
}

} // namespace quantumflow::pipeline
