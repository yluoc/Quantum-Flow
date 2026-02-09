#include "ws/json_serializer.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace quantumflow {

std::string serialize_book(const BookSnapshot& snapshot) {
    json bids = json::array();
    for (const auto& lvl : snapshot.bids) {
        bids.push_back({
            {"price", lvl.price},
            {"quantity", lvl.quantity},
            {"order_count", lvl.order_count}
        });
    }

    json asks = json::array();
    for (const auto& lvl : snapshot.asks) {
        asks.push_back({
            {"price", lvl.price},
            {"quantity", lvl.quantity},
            {"order_count", lvl.order_count}
        });
    }

    json msg = {
        {"type", "book"},
        {"timestamp_ns", snapshot.timestamp_ns},
        {"data", {
            {"symbol", snapshot.symbol},
            {"best_bid", snapshot.best_bid},
            {"best_ask", snapshot.best_ask},
            {"mid_price", snapshot.mid_price},
            {"bids", bids},
            {"asks", asks}
        }}
    };

    return msg.dump();
}

std::string serialize_trades(const std::vector<TradeInfo>& trades, uint64_t timestamp_ns) {
    json arr = json::array();
    // Send most recent 50 trades
    size_t start = trades.size() > 50 ? trades.size() - 50 : 0;
    for (size_t i = start; i < trades.size(); ++i) {
        const auto& t = trades[i];
        arr.push_back({
            {"price", t.price},
            {"quantity", t.quantity},
            {"side", t.side},
            {"timestamp_ns", t.timestamp_ns}
        });
    }

    json msg = {
        {"type", "trades"},
        {"timestamp_ns", timestamp_ns},
        {"data", {
            {"trades", arr}
        }}
    };

    return msg.dump();
}

std::string serialize_latency(const LatencySnapshot& latency, uint64_t timestamp_ns) {
    json msg = {
        {"type", "latency"},
        {"timestamp_ns", timestamp_ns},
        {"data", {
            {"python_to_cpp_us", latency.python_to_cpp_us},
            {"order_match_us", latency.order_match_us},
            {"strategy_eval_us", latency.strategy_eval_us},
            {"ws_broadcast_us", latency.ws_broadcast_us},
            {"total_us", latency.total_us}
        }}
    };

    return msg.dump();
}

std::string serialize_strategies(
    const std::unordered_map<std::string, StrategySignal>& signals,
    uint64_t timestamp_ns)
{
    json arr = json::array();
    for (const auto& [name, sig] : signals) {
        arr.push_back({
            {"strategy_name", sig.strategy_name},
            {"symbol", sig.symbol},
            {"signal", signal_to_string(sig.signal)},
            {"confidence", sig.confidence},
            {"timestamp_ns", sig.timestamp_ns}
        });
    }

    json msg = {
        {"type", "strategies"},
        {"timestamp_ns", timestamp_ns},
        {"data", {
            {"signals", arr}
        }}
    };

    return msg.dump();
}

} // namespace quantumflow
