#pragma once

#include "strategies/strategy_base.hpp"
#include <cmath>
#include <algorithm>

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

        const double bid_strength = iceberg_strength(recent_trades, snapshot.best_bid);
        const double ask_strength = iceberg_strength(recent_trades, snapshot.best_ask);
        const bool iceberg_bid = bid_strength > 1.0;
        const bool iceberg_ask = ask_strength > 1.0;

        if (iceberg_bid && !iceberg_ask) return Signal::BUY;
        if (iceberg_ask && !iceberg_bid) return Signal::SELL;
        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot& snapshot,
                      const std::vector<TradeInfo>& recent_trades,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL || recent_trades.empty() || snapshot.bids.empty())
            return 0.0;

        const double bid_strength = iceberg_strength(recent_trades, snapshot.best_bid);
        const double ask_strength = iceberg_strength(recent_trades, snapshot.best_ask);

        const double side_strength = (signal == Signal::BUY) ? bid_strength : ask_strength;
        const double opp_strength = (signal == Signal::BUY) ? ask_strength : bid_strength;

        const double side_score = clamp_confidence(side_strength - 1.0);
        const double opp_score = clamp_confidence(opp_strength - 1.0);
        return clamp_confidence(side_score * (1.0 - opp_score));
    }

private:
    int min_fills_;
    uint64_t min_volume_;
    double price_tolerance_;

    double iceberg_strength(const std::vector<TradeInfo>& trades,
                            double price_level) const {
        int fill_count = 0;
        uint64_t total_volume = 0;

        for (const auto& trade : trades) {
            if (std::abs(trade.price - price_level) < price_tolerance_) {
                fill_count++;
                total_volume += trade.quantity;
            }
        }

        const double fill_den = static_cast<double>(std::max(1, min_fills_));
        const double vol_den = static_cast<double>(std::max<uint64_t>(1, min_volume_));
        const double fill_ratio = static_cast<double>(fill_count) / fill_den;
        const double vol_ratio = static_cast<double>(total_volume) / vol_den;
        return std::min(fill_ratio, vol_ratio);
    }
};

} // namespace quantumflow
