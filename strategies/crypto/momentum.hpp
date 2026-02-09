#pragma once

#include "strategies/strategy_base.hpp"
#include <deque>

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

        double returns = (price_history_.back() - price_history_.front())
                         / price_history_.front();

        if (returns > threshold_) return Signal::BUY;
        if (returns < -threshold_) return Signal::SELL;
        return Signal::NEUTRAL;
    }

    void reset() override { price_history_.clear(); }

private:
    size_t window_;
    double threshold_;
    std::deque<double> price_history_;
};

} // namespace quantumflow
