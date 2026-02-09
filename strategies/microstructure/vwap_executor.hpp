#pragma once

#include "strategies/strategy_base.hpp"
#include <vector>
#include <numeric>

namespace quantumflow {

class VWAPExecutor : public Strategy {
public:
    explicit VWAPExecutor(uint64_t total_quantity = 0,
                          uint64_t time_horizon_ms = 60000,
                          std::vector<double> volume_profile = {})
        : total_quantity_(total_quantity)
        , time_horizon_ms_(time_horizon_ms)
        , volume_profile_(std::move(volume_profile))
    {
        if (volume_profile_.empty()) {
            // Uniform profile by default (1-second slices)
            size_t slices = time_horizon_ms_ / 1000;
            if (slices == 0) slices = 1;
            volume_profile_.assign(slices, 1.0 / static_cast<double>(slices));
        }
    }

    const std::string& name() const override {
        static const std::string n = "VWAPExecutor";
        return n;
    }

    Signal evaluate(const BookSnapshot& snapshot,
                    const std::vector<TradeInfo>&) override {
        if (total_quantity_ == 0 || executed_quantity_ >= total_quantity_)
            return Signal::NEUTRAL;

        // Determine current slice
        size_t current_slice = elapsed_ms_ / 1000;
        if (current_slice >= volume_profile_.size())
            return Signal::NEUTRAL;

        double target_fraction = 0.0;
        for (size_t i = 0; i <= current_slice; ++i)
            target_fraction += volume_profile_[i];

        uint64_t target_qty = static_cast<uint64_t>(
            static_cast<double>(total_quantity_) * target_fraction);

        if (executed_quantity_ < target_qty) return Signal::BUY;
        return Signal::NEUTRAL;
    }

    void on_trade(const TradeInfo& trade) override {
        executed_quantity_ += trade.quantity;
    }

    void reset() override {
        executed_quantity_ = 0;
        elapsed_ms_ = 0;
    }

    void advance_time(uint64_t delta_ms) { elapsed_ms_ += delta_ms; }

private:
    uint64_t total_quantity_;
    uint64_t time_horizon_ms_;
    std::vector<double> volume_profile_;
    uint64_t executed_quantity_ = 0;
    uint64_t elapsed_ms_ = 0;
};

} // namespace quantumflow
