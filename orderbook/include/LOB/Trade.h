#ifndef LOB_TRADE_H
#define LOB_TRADE_H

#include "Types.h"
#include <vector>
#include <iostream>

namespace quantumflow::lob {

/**
 * Trade: Represents a single executed trade between two orders.
 *
 * Created during matching when an incoming order crosses a resting order.
 * Immutable record - all fields are set at construction.
 *
 * Invariants:
 * - incoming_order and matched_order refer to the two orders that crossed
 * - trade_price is the price at which the trade executed (resting order's price)
 * - trade_volume is the number of shares exchanged (> 0)
 */
class Trade {
    private:
        ID incoming_order; /**< Id of the incoming (aggressing) order */
        ID matched_order; /**< Id of the resting (matched) order */
        PRICE trade_price; /**< Price at which the trade executed */
        Volume trade_volume; /**< Volume/number of shares exchanged */
    public:
        /**
         * @brief Constructs a trade record from a matched pair of orders
         * @param incoming_order id of the incoming (aggressing) order
         * @param matched_order id of the resting (matched) order
         * @param trade_price price at which the trade executed
         * @param trade_volume number of shares exchanged
         */
        Trade(
            ID incoming_order,
            ID matched_order,
            PRICE trade_price,
            Volume trade_volume)
            : 
            incoming_order(incoming_order), 
            matched_order(matched_order), 
            trade_price(trade_price), 
            trade_volume(trade_volume) 
        {}
        
        /** Getters */
        ID get_incoming_order() const { return incoming_order; }
        ID get_matched_order() const { return matched_order; }
        PRICE get_trade_price() const { return trade_price; }
        Volume get_trade_volume() const { return trade_volume; }

        /** Print trade details */
        void print() const {
            std::cout << "Trade Details:" << std::endl;
            std::cout << "Incoming Order ID: " << incoming_order << std::endl;
            std::cout << "Matched Order ID: " << matched_order << std::endl;
            std::cout << "Trade Price: " << trade_price << std::endl;
            std::cout << "Trade Volume: " << trade_volume << std::endl;
        }

};

using Trades = std::vector<Trade>;

} // namespace quantumflow::lob

#endif // LOB_TRADE_H