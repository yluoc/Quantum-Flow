#ifndef LOB_TRADE_H
#define LOB_TRADE_H

#include "Types.h"
#include <vector>
#include <iostream>

class Trade {
    private:
        ID incoming_order;
        ID matched_order;
        PRICE trade_price;
        Volume trade_volume;
    public:
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

#endif // LOB_TRADE_H