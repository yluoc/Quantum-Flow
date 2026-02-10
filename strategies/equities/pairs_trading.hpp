#pragma once

#include "strategies/strategy_base.hpp"
#include <deque>
#include <cmath>
#include <numeric>
#include <algorithm>

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
        double z_score = 0.0;
        if (!compute_z_score(z_score)) return Signal::NEUTRAL;

        if (z_score > z_threshold_) return Signal::SHORT_PAIR;
        if (z_score < -z_threshold_) return Signal::LONG_PAIR;
        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot&,
                      const std::vector<TradeInfo>&,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL) return 0.0;

        double z_score = 0.0;
        if (!compute_z_score(z_score)) return 0.0;

        const double threshold = std::max(std::abs(z_threshold_), 1e-9);
        const double excess = std::abs(z_score) - threshold;
        return clamp_confidence(excess / threshold);
    }

    void reset() override {
        spread_history_.clear();
        price1_ = 0.0;
        price2_ = 0.0;
    }

private:
    bool compute_z_score(double& out_z_score) const {
        if (spread_history_.size() < window_) return false;

        const double mean = std::accumulate(spread_history_.begin(),
                                            spread_history_.end(), 0.0)
                            / static_cast<double>(spread_history_.size());

        double sq_sum = 0.0;
        for (double s : spread_history_) {
            const double diff = s - mean;
            sq_sum += diff * diff;
        }
        const double std_dev = std::sqrt(sq_sum / static_cast<double>(spread_history_.size()));
        if (std_dev < 1e-12) return false;

        const double current_spread = spread_history_.back();
        out_z_score = (current_spread - mean) / std_dev;
        return true;
    }

    double beta_;
    size_t window_;
    double z_threshold_;
    double price1_ = 0.0;
    double price2_ = 0.0;
    std::deque<double> spread_history_;
};

} // namespace quantumflow
