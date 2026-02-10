#pragma once

#include "strategies/strategy_base.hpp"
#include <utility>
#include <cmath>

namespace quantumflow {

class MarketMaker : public Strategy {
public:
    explicit MarketMaker(double max_inventory = 10.0, double base_spread = 0.001)
        : max_inventory_(max_inventory), base_spread_(base_spread) {}

    const std::string& name() const override {
        static const std::string n = "MarketMaker";
        return n;
    }

    Signal evaluate(const BookSnapshot& snapshot,
                    const std::vector<TradeInfo>&) override {
        if (snapshot.mid_price <= 0.0) return Signal::NEUTRAL;

        double inventory_ratio = inventory_ratio_abs_signed();

        if (inventory_ratio > 0.5) return Signal::SELL;
        if (inventory_ratio < -0.5) return Signal::BUY;
        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot&,
                      const std::vector<TradeInfo>&,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL) return 0.0;

        const double ratio = std::abs(inventory_ratio_abs_signed());
        const double excess = ratio - 0.5;
        return clamp_confidence(excess / 0.5);
    }

    void on_trade(const TradeInfo& trade) override {
        if (trade.side == 0) // buy fill
            inventory_ += static_cast<double>(trade.quantity);
        else
            inventory_ -= static_cast<double>(trade.quantity);
    }

    void reset() override { inventory_ = 0.0; }

    /// Generate bid/ask quotes around mid price.
    std::pair<double, double> generate_quotes(double mid_price) const {
        double skew = (inventory_ / max_inventory_) * 0.001;
        double half_spread = mid_price * base_spread_ / 2.0;
        double bid = mid_price - half_spread - skew;
        double ask = mid_price + half_spread - skew;
        return {bid, ask};
    }

private:
    double inventory_ratio_abs_signed() const {
        if (std::abs(max_inventory_) < 1e-12) return 0.0;
        return inventory_ / max_inventory_;
    }

    double max_inventory_;
    double base_spread_;
    double inventory_ = 0.0;
};

} // namespace quantumflow
