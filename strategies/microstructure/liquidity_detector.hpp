#pragma once

#include "strategies/strategy_base.hpp"
#include <cmath>

namespace quantumflow {

class LiquidityDetector : public Strategy {
public:
    explicit LiquidityDetector(int min_fills = 5, uint64_t min_volume = 100,
                               double price_tolerance = 0.01)
        : min_fills_(min_fills), min_volume_(min_volume),
          price_tolerance_(price_tolerance) {}

    const std::string& name() const override {
        static const std::string n = "LiquidityDetector";
        return n;
    }

    Signal evaluate(const BookSnapshot& snapshot,
                    const std::vector<TradeInfo>& recent_trades) override {
        if (recent_trades.empty() || snapshot.bids.empty())
            return Signal::NEUTRAL;

        // Check for iceberg orders near best bid and best ask
        bool iceberg_bid = detect_iceberg(recent_trades, snapshot.best_bid);
        bool iceberg_ask = detect_iceberg(recent_trades, snapshot.best_ask);

        // Large hidden bid support → BUY signal
        if (iceberg_bid && !iceberg_ask) return Signal::BUY;
        // Large hidden ask pressure → SELL signal
        if (iceberg_ask && !iceberg_bid) return Signal::SELL;
        return Signal::NEUTRAL;
    }

private:
    int min_fills_;
    uint64_t min_volume_;
    double price_tolerance_;

    bool detect_iceberg(const std::vector<TradeInfo>& trades,
                        double price_level) const {
        int fill_count = 0;
        uint64_t total_volume = 0;

        for (const auto& trade : trades) {
            if (std::abs(trade.price - price_level) < price_tolerance_) {
                fill_count++;
                total_volume += trade.quantity;
            }
        }
        return fill_count > min_fills_ && total_volume > min_volume_;
    }
};

} // namespace quantumflow
