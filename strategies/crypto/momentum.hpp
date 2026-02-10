#pragma once

#include "strategies/strategy_base.hpp"
#include <deque>
#include <cmath>
#include <algorithm>

namespace quantumflow {

class MomentumStrategy : public Strategy {
public:
    explicit MomentumStrategy(size_t window = 20, double threshold = 0.02)
        : window_(window), threshold_(threshold) {}

    const std::string& name() const override {
        static const std::string n = "Momentum";
        return n;
    }

    Signal evaluate(const BookSnapshot& snapshot,
                    const std::vector<TradeInfo>&) override {
        if (snapshot.mid_price <= 0.0) return Signal::NEUTRAL;

        price_history_.push_back(snapshot.mid_price);
        if (price_history_.size() > window_) {
            price_history_.pop_front();
        }
        if (price_history_.size() < 2) return Signal::NEUTRAL;

        const double returns = current_return();

        if (returns > threshold_) return Signal::BUY;
        if (returns < -threshold_) return Signal::SELL;
        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot&,
                      const std::vector<TradeInfo>&,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL || price_history_.size() < 2) return 0.0;

        const double threshold = std::max(std::abs(threshold_), 1e-9);
        const double excess = std::abs(current_return()) - threshold;
        return clamp_confidence(excess / threshold);
    }

    void reset() override { price_history_.clear(); }

private:
    double current_return() const {
        if (price_history_.size() < 2 || std::abs(price_history_.front()) < 1e-12) {
            return 0.0;
        }
        return (price_history_.back() - price_history_.front()) / price_history_.front();
    }

    size_t window_;
    double threshold_;
    std::deque<double> price_history_;
};

} // namespace quantumflow
