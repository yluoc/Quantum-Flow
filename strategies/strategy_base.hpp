#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "common/signal_types.hpp"
#include "common/price_converter.hpp"
#include "LOB/Book.h"

namespace quantumflow {

struct PriceLevel {
    double price;
    uint64_t quantity;
    uint64_t order_count;
};

struct BookSnapshot {
    std::string symbol;
    std::vector<PriceLevel> bids; // descending by price
    std::vector<PriceLevel> asks; // ascending by price
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
    uint8_t side; // 0=buy, 1=sell
    uint64_t timestamp_ns;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual const std::string& name() const = 0;
    virtual Signal evaluate(const BookSnapshot& snapshot,
                            const std::vector<TradeInfo>& recent_trades) = 0;
    virtual void on_trade(const TradeInfo& trade) { (void)trade; }
    virtual void reset() {}
};

} // namespace quantumflow
