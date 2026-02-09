#pragma once

#include "strategies/strategy_base.hpp"
#include <deque>
#include <cmath>
#include <numeric>

namespace quantumflow {

class PairsTrading : public Strategy {
public:
    explicit PairsTrading(double beta = 1.0, size_t window = 50,
                          double z_threshold = 2.0)
        : beta_(beta), window_(window), z_threshold_(z_threshold) {}

    const std::string& name() const override {
        static const std::string n = "PairsTrading";
        return n;
    }

    void update_prices(double price1, double price2) {
        price1_ = price1;
        price2_ = price2;
        double spread = price1_ - beta_ * price2_;
        spread_history_.push_back(spread);
        if (spread_history_.size() > window_)
            spread_history_.pop_front();
    }

    Signal evaluate(const BookSnapshot&,
                    const std::vector<TradeInfo>&) override {
        if (spread_history_.size() < window_) return Signal::NEUTRAL;

        double mean = std::accumulate(spread_history_.begin(),
                                      spread_history_.end(), 0.0)
                      / static_cast<double>(spread_history_.size());

        double sq_sum = 0.0;
        for (double s : spread_history_) {
            double diff = s - mean;
            sq_sum += diff * diff;
        }
        double std_dev = std::sqrt(sq_sum / static_cast<double>(spread_history_.size()));
        if (std_dev < 1e-12) return Signal::NEUTRAL;

        double current_spread = spread_history_.back();
        double z_score = (current_spread - mean) / std_dev;

        if (z_score > z_threshold_) return Signal::SHORT_PAIR;
        if (z_score < -z_threshold_) return Signal::LONG_PAIR;
        return Signal::NEUTRAL;
    }

    void reset() override {
        spread_history_.clear();
        price1_ = 0.0;
        price2_ = 0.0;
    }

private:
    double beta_;
    size_t window_;
    double z_threshold_;
    double price1_ = 0.0;
    double price2_ = 0.0;
    std::deque<double> spread_history_;
};

} // namespace quantumflow
