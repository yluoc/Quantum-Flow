#ifndef LOB_BOOK_H
#define LOB_BOOK_H

#include <vector>
#include "Level.h"
#include "SlabPool.h"
#include "FlatHashMap.h"

using PriceLevelMap = FlatHashMap<PRICE, Level*>;
using Orders = FlatHashMap<ID, Order*>;

/**
 * Book: High-performance limit order book matching engine.
 *
 * Design:
 * - Uses SlabPool for zero-allocation hot path (after warmup)
 * - Raw pointers internally (no shared_ptr overhead)
 * - Intrusive sorted level lists for O(1) best price access
 * - Intrusive FIFO lists at each price level
 *
 * Invariants:
 * - best_bid points to highest buy price level (or nullptr)
 * - best_ask points to lowest sell price level (or nullptr)
 * - buy_list_head is the highest buy level; levels linked in descending price order
 * - sell_list_head is the lowest sell level; levels linked in ascending price order
 * - All orders/levels owned by internal pools
 * - id_to_order only contains resting orders
 */
class Book {
    private:
        // Price level maps (price -> Level*)
        PriceLevelMap buy_side_limits;
        PriceLevelMap sell_side_limits;

        // Intrusive sorted level list heads
        Level* buy_list_head;   // highest buy level (descending order)
        Level* sell_list_head;  // lowest sell level (ascending order)

        // Cached best prices (aliases for list heads)
        Level*& best_bid;
        Level*& best_ask;

        // Order lookup (only for resting orders)
        Orders id_to_order;

        // Memory pools (own all orders and levels)
        SlabPool<Order, 16384> order_pool;
        SlabPool<Level, 1024> level_pool;

        // Trade output buffer
        static constexpr size_t TRADE_BUFFER_SIZE = 16;
        mutable std::vector<Trade> trade_buffer;

        Level* get_or_create_level(PRICE price, bool is_buy);
        bool match_against_level(Order* incoming_order, Level* level);
        void insert_resting_order(Order* order);
        void remove_order_from_level(Order* order, bool is_buy);

        // Intrusive sorted list helpers
        void insert_level_sorted_buy(Level* level);
        void insert_level_sorted_sell(Level* level);
        void remove_level_from_buy_list(Level* level);
        void remove_level_from_sell_list(Level* level);

    public:
        explicit Book(size_t initial_capacity = 1024);
        ~Book() = default;

        Book(const Book&) = delete;
        Book& operator=(const Book&) = delete;

        const Trades& place_order(
            ID order_id,
            ID agent_id,
            OrderType order_type,
            PRICE price,
            Volume volume
        );

        void delete_order(ID id);

        PRICE get_spread() const;
        double get_mid_price() const;
        PRICE get_best_buy() const;
        PRICE get_best_sell() const;

        size_t get_buy_levels_count() const { return buy_side_limits.size(); }
        size_t get_sell_levels_count() const { return sell_side_limits.size(); }
        size_t get_resting_orders_count() const { return id_to_order.size(); }

        PriceLevelMap& get_buy_limits() { return buy_side_limits; }
        PriceLevelMap& get_sell_limits() { return sell_side_limits; }
        Orders& get_id_to_order() { return id_to_order; }

        std::vector<PRICE> get_buy_prices() const;
        std::vector<PRICE> get_sell_prices() const;

        void print() const;
        OrderStatus get_order_status(ID id) const;
};

#endif // LOB_BOOK_H
