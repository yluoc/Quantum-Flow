#pragma once

#include "strategies/strategy_base.hpp"
#include <cmath>

namespace quantumflow {

class FundingArbitrage : public Strategy {
public:
    explicit FundingArbitrage(double funding_threshold = 0.001)
        : threshold_(funding_threshold) {}

    const std::string& name() const override {
        static const std::string n = "FundingArbitrage";
        return n;
    }

    void set_funding_rate(double rate) { funding_rate_ = rate; }
    void set_spot_price(double price) { spot_price_ = price; }
    void set_perp_price(double price) { perp_price_ = price; }

    Signal evaluate(const BookSnapshot&,
                    const std::vector<TradeInfo>&) override {
        // When funding > threshold, shorts pay longs → go long spot, short perp
        if (funding_rate_ > threshold_)
            return Signal::LONG_SPOT_SHORT_PERP;

        // When funding < -threshold, longs pay shorts → short spot, long perp
        if (funding_rate_ < -threshold_)
            return Signal::SHORT_SPOT_LONG_PERP;

        return Signal::NEUTRAL;
    }

    void reset() override {
        funding_rate_ = 0.0;
        spot_price_ = 0.0;
        perp_price_ = 0.0;
    }

private:
    double threshold_;
    double funding_rate_ = 0.0;
    double spot_price_ = 0.0;
    double perp_price_ = 0.0;
};

} // namespace quantumflow
