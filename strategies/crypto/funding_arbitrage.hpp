#pragma once

#include "strategies/strategy_base.hpp"
#include <cmath>
#include <algorithm>

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
        if (funding_rate_ > threshold_)
            return Signal::LONG_SPOT_SHORT_PERP;

        if (funding_rate_ < -threshold_)
            return Signal::SHORT_SPOT_LONG_PERP;

        return Signal::NEUTRAL;
    }

    double confidence(const BookSnapshot&,
                      const std::vector<TradeInfo>&,
                      Signal signal) const override {
        if (signal == Signal::NEUTRAL) return 0.0;

        const double threshold = std::max(std::abs(threshold_), 1e-9);
        const double funding_excess = std::abs(funding_rate_) - threshold;
        const double funding_score = clamp_confidence(funding_excess / threshold);

        double basis_score = 0.0;
        if (spot_price_ > 1e-9 && perp_price_ > 1e-9) {
            const double basis = std::abs(perp_price_ - spot_price_) / spot_price_;
            basis_score = clamp_confidence(basis / 0.01); // 1% basis -> max contribution
        }

        return clamp_confidence(0.7 * funding_score + 0.3 * basis_score);
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
