#pragma once

#include "strategies/strategy_base.hpp"
#include <algorithm>
#include <cmath>

namespace quantumflow {

class OrderBookImbalance : public Strategy {
public:
    explicit OrderBookImbalance(size_t top_n = 5, double threshold = 0.3)
        : top_n_(top_n), threshold_(threshold) {}

    const std::string& name() const override {
        static const std::string n = "OrderBookImbalance";
        return n;
    }

    Signal evaluate(const BookSnapshot& snapshot,
                    const std::vector<TradeInfo>&) override {
        double imbalance = compute_imbalance(snapshot);

        if (imbalance > threshold_) return Signal::BUY;
        if (imbalance < -threshold_) return Signal::SELL;
        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot& snapshot,
                      const std::vector<TradeInfo>&,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL) return 0.0;

        const double imbalance = std::abs(compute_imbalance(snapshot));
        const double threshold = std::max(std::abs(threshold_), 1e-9);
        const double excess = imbalance - threshold;
        return clamp_confidence(excess / threshold);
    }

private:
    double compute_imbalance(const BookSnapshot& snapshot) const {
        double bid_volume = 0.0;
        double ask_volume = 0.0;

        size_t bid_count = std::min(top_n_, snapshot.bids.size());
        size_t ask_count = std::min(top_n_, snapshot.asks.size());

        for (size_t i = 0; i < bid_count; ++i)
            bid_volume += static_cast<double>(snapshot.bids[i].quantity);
        for (size_t i = 0; i < ask_count; ++i)
            ask_volume += static_cast<double>(snapshot.asks[i].quantity);

        double total = bid_volume + ask_volume;
        if (total < 1e-9) return 0.0;

        return (bid_volume - ask_volume) / total;
    }

    size_t top_n_;
    double threshold_;
};

} // namespace quantumflow
