#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "common/signal_types.hpp"
#include "common/price_converter.hpp"
#include "LOB/Book.h"

namespace quantumflow {

inline double clamp_confidence(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

struct PriceLevel {
    double price;
    uint64_t quantity;
    uint64_t order_count;
};

struct BookSnapshot {
    std::string symbol;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    double best_bid;
    double best_ask;
    double mid_price;
    uint64_t timestamp_ns;

    static BookSnapshot from_book(const Book& book, const std::string& symbol,
                                  const PriceConverter& converter);
};

struct TradeInfo {
    double price;
    uint64_t quantity;
    uint8_t side;
    uint64_t timestamp_ns;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual const std::string& name() const = 0;
    virtual Signal evaluate(const BookSnapshot& snapshot,
                            const std::vector<TradeInfo>& recent_trades) = 0;
    virtual double confidence(const BookSnapshot& snapshot,
                              const std::vector<TradeInfo>& recent_trades,
                              Signal signal) const {
        (void)snapshot;
        (void)recent_trades;
        return signal == Signal::NEUTRAL ? 0.0 : 0.5;
    }
    virtual void on_trade(const TradeInfo& trade) { (void)trade; }
    virtual void reset() {}
};

} // namespace quantumflow
