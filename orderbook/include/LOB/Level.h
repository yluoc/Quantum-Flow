#ifndef LOB_LEVEL_H
#define LOB_LEVEL_H

#include "Order.h"
#include "Trade.h"

/**
 * Level: Represents a price level in the order book.
 * 
 * Maintains orders in FIFO order using intrusive doubly-linked list.
 * Uses raw pointers - orders are owned by Book's SlabPool.
 * 
 * Invariants:
 * - head points to oldest order (first to match), tail to newest
 * - If level is non-empty, head and tail are non-null
 * - total_volume == sum of remaining_volume of all orders
 * - order_number == count of orders in the list
 */
class Level {
    private:
        PRICE limit_price; /**< Limit price */
        Length order_number; /**< Number of orders at this limit */
        Volume total_volume; /**< Total volume at this limit */

        Order* head; /**< First order in the list (oldest, FIFO) */
        Order* tail; /**< Last order in the list (most recent) */

        Level* prev_level; /**< Previous level in sorted intrusive list */
        Level* next_level; /**< Next level in sorted intrusive list */

    public:
        Level(PRICE price):
            limit_price(price),
            order_number(0),
            total_volume(0),
            head(nullptr),
            tail(nullptr),
            prev_level(nullptr),
            next_level(nullptr)
        {}
        
        /**
         * @brief Inserts an order at the end of the FIFO queue (push_back)
         * @param order pointer to the order to insert (must be non-null)
         */
        void push_back(Order* order);
        
        /**
         * @brief Removes the first order from the queue (pop_front)
         * @return pointer to the removed order, or nullptr if empty
         */
        Order* pop_front();
        
        /**
         * @brief Removes an order from anywhere in the queue
         * @param order pointer to the order to remove (must be in this level)
         */
        void erase(Order* order);
        
        /**
         * @brief Decreases total volume (used during matching)
         * @param volume volume to subtract
         */
        void decrease_volume(Volume volume) {
            total_volume -= volume;
        }
        
        /**
         * @brief Checks if the level is empty (i.e. no orders)
         * @return true if there are no orders, false otherwise
         */
        bool is_empty() const { return order_number == 0; }

        /** Getters */
        PRICE get_price() const { return limit_price; }
        Length get_order_number() const { return order_number; }
        Volume get_total_volume() const { return total_volume; }
        
        Order* get_head() const { return head; }
        Order* get_tail() const { return tail; }

        Level* get_prev_level() const { return prev_level; }
        void set_prev_level(Level* p) { prev_level = p; }
        Level* get_next_level() const { return next_level; }
        void set_next_level(Level* n) { next_level = n; }

        /** Print method (for debugging) */
        void print() const;
};

// Raw pointer type alias
using LevelPointer = Level*;

#endif //LOB_LEVEL_H
