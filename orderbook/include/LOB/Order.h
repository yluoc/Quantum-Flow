#ifndef LOB_ORDER_H
#define LOB_ORDER_H

#include "Types.h"

// Forward declaration
class Order;

/**
 * Order: Represents a limit order in the order book.
 * 
 * Uses intrusive linked list (raw pointers) for FIFO ordering at same price level.
 * Orders are owned by the Book's SlabPool, not by shared_ptr.
 * 
 * Invariants:
 * - prev_order and next_order are either nullptr or point to valid Orders in the same Level
 * - Order lifetime is managed by Book's SlabPool
 */
class Order {
    private:
        ID order_id; /**< Order id */
        ID agent_id; /**< id of the agent who placed the order */
        OrderType order_type; /**< Order type (buy/sell) */
        PRICE order_price; /**< Limit price of the order */
        Volume initial_volume; /**< Initial volume/number of shares in the order */
        Volume remaining_volume; /**< Volume/number of remaining shares in the order */
        OrderStatus order_status; /**< Current status of the order */
        
        /** Intrusive doubly-linked list for FIFO ordering at same price level */
        Order* prev_order; /**< Previous order in the list (nullptr if first) */
        Order* next_order; /**< Next order in the list (nullptr if last) */
        
    public:
        Order(
            ID order_id,
            ID agent_id,
            OrderType order_type,
            PRICE order_price,
            Volume initial_volume,
            Volume remaining_volume,
            OrderStatus order_status)
            :
            order_id(order_id),
            agent_id(agent_id),
            order_type(order_type),
            order_price(order_price),
            initial_volume(initial_volume),
            remaining_volume(remaining_volume),
            order_status(order_status),
            prev_order(nullptr),
            next_order(nullptr) 
        {}
        
        /**
         * @brief fills the order with a given volume (quantity)
         * @param fill_volume: Volume to fill
         */
        void fill(Volume fill_volume);

        /**
         * @brief checks if the order is fulfilled
         * @return true if the order is fulfilled, false otherwise
         */
        bool is_fulfilled() const;

        /** Getters and setters */
        ID get_order_id() const;
        ID get_agent_id() const;
        OrderType get_order_type() const;
        PRICE get_order_price() const;
        Volume get_initial_volume() const;
        Volume get_remaining_volume() const;
        OrderStatus get_order_status() const;

        void set_order_status(OrderStatus order_status);
        
        // Intrusive list accessors (for Level class)
        Order* get_prev_order() const { return prev_order; }
        void set_prev_order(Order* prev) { prev_order = prev; }
        Order* get_next_order() const { return next_order; }
        void set_next_order(Order* next) { next_order = next; }

        /** Print order details */
        void print();
};

// Raw pointer type alias for clarity
using OrderPointer = Order*;

#endif // LOB_ORDER_H