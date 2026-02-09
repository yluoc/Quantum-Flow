#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "strategies/strategy_base.hpp"
#include "common/latency_snapshot.hpp"
#include "common/signal_types.hpp"

namespace quantumflow {

/// Serialize a BookSnapshot to the WebSocket JSON protocol.
/// { "type": "book", "timestamp_ns": N, "data": {...} }
std::string serialize_book(const BookSnapshot& snapshot);

/// Serialize recent trades to the WebSocket JSON protocol.
/// { "type": "trades", "timestamp_ns": N, "data": { "trades": [...] } }
std::string serialize_trades(const std::vector<TradeInfo>& trades, uint64_t timestamp_ns);

/// Serialize a LatencySnapshot to the WebSocket JSON protocol.
/// { "type": "latency", "timestamp_ns": N, "data": {...} }
std::string serialize_latency(const LatencySnapshot& latency, uint64_t timestamp_ns);

/// Serialize strategy signals to the WebSocket JSON protocol.
/// { "type": "strategies", "timestamp_ns": N, "data": { "signals": [...] } }
std::string serialize_strategies(
    const std::unordered_map<std::string, StrategySignal>& signals,
    uint64_t timestamp_ns);

} // namespace quantumflow
