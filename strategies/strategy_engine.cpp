#include "strategies/strategy_engine.hpp"
#include <chrono>

namespace quantumflow {

void StrategyEngine::add_strategy(std::unique_ptr<Strategy> strategy) {
    strategies_.push_back(std::move(strategy));
}

std::vector<StrategySignal> StrategyEngine::evaluate(
    const BookSnapshot& snapshot,
    const std::vector<TradeInfo>& recent_trades
) {
    std::vector<StrategySignal> signals;
    signals.reserve(strategies_.size());

    uint64_t now_ns = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    for (auto& strat : strategies_) {
        Signal sig = strat->evaluate(snapshot, recent_trades);
        StrategySignal ss;
        ss.strategy_name = strat->name();
        ss.symbol = snapshot.symbol;
        ss.signal = sig;
        ss.confidence = 1.0;
        ss.timestamp_ns = now_ns;

        latest_signals_[ss.strategy_name] = ss;
        signals.push_back(std::move(ss));
    }
    return signals;
}

void StrategyEngine::on_trade(const TradeInfo& trade) {
    for (auto& strat : strategies_) {
        strat->on_trade(trade);
    }
}

const StrategySignal* StrategyEngine::latest_signal(const std::string& name) const {
    auto it = latest_signals_.find(name);
    return it != latest_signals_.end() ? &it->second : nullptr;
}

} // namespace quantumflow
