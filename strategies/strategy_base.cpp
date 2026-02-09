#include "strategies/strategy_base.hpp"

namespace quantumflow {

BookSnapshot BookSnapshot::from_book(const Book& book, const std::string& sym,
                                     const PriceConverter& converter) {
    BookSnapshot snap;
    snap.symbol = sym;
    snap.timestamp_ns = 0;

    // Walk buy prices (sorted descending by Book invariant)
    auto buy_prices = book.get_buy_prices();
    auto& buy_limits = const_cast<Book&>(book).get_buy_limits();
    snap.bids.reserve(buy_prices.size());
    for (PRICE p : buy_prices) {
        auto it = buy_limits.find(p);
        if (it != buy_limits.end()) {
            Level* lvl = it->second;
            snap.bids.push_back({
                converter.to_external(p),
                lvl->get_total_volume(),
                lvl->get_order_number()
            });
        }
    }

    // Walk sell prices (sorted ascending by Book invariant)
    auto sell_prices = book.get_sell_prices();
    auto& sell_limits = const_cast<Book&>(book).get_sell_limits();
    snap.asks.reserve(sell_prices.size());
    for (PRICE p : sell_prices) {
        auto it = sell_limits.find(p);
        if (it != sell_limits.end()) {
            Level* lvl = it->second;
            snap.asks.push_back({
                converter.to_external(p),
                lvl->get_total_volume(),
                lvl->get_order_number()
            });
        }
    }

    snap.best_bid = book.get_buy_levels_count() > 0
                        ? converter.to_external(book.get_best_buy())
                        : 0.0;
    snap.best_ask = book.get_sell_levels_count() > 0
                        ? converter.to_external(book.get_best_sell())
                        : 0.0;
    snap.mid_price = book.get_mid_price() > 0
                         ? converter.to_external(static_cast<PRICE>(book.get_mid_price()))
                         : 0.0;

    return snap;
}

} // namespace quantumflow
