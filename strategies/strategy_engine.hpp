#pragma once

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "strategies/strategy_base.hpp"

namespace quantumflow {

class StrategyEngine {
public:
    void add_strategy(std::unique_ptr<Strategy> strategy);

    /// Run all strategies against the given snapshot and return signals.
    std::vector<StrategySignal> evaluate(const BookSnapshot& snapshot,
                                         const std::vector<TradeInfo>& recent_trades);

    /// Notify all strategies of a new trade.
    void on_trade(const TradeInfo& trade);

    /// Get the latest signal for a given strategy name.
    const StrategySignal* latest_signal(const std::string& strategy_name) const;

    /// Get all latest signals.
    const std::unordered_map<std::string, StrategySignal>& all_signals() const {
        return latest_signals_;
    }

    size_t strategy_count() const { return strategies_.size(); }

private:
    std::vector<std::unique_ptr<Strategy>> strategies_;
    std::unordered_map<std::string, StrategySignal> latest_signals_;
};

} // namespace quantumflow
