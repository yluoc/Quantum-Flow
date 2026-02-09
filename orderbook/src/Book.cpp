#include "LOB/Book.h"
#include "LOB/Macros.h"
#include <algorithm>
#include <iostream>
#include <limits>

Book::Book(size_t initial_capacity)
    : buy_list_head(nullptr),
      sell_list_head(nullptr),
      best_bid(buy_list_head),
      best_ask(sell_list_head),
      order_pool(initial_capacity),
      level_pool(initial_capacity / 16) {
    trade_buffer.reserve(TRADE_BUFFER_SIZE);

    buy_side_limits.reserve(256);
    sell_side_limits.reserve(256);
    id_to_order.reserve(initial_capacity);
}

// --- Intrusive sorted list helpers ---

// Buy list: descending price order (head = highest)
void Book::insert_level_sorted_buy(Level* level) {
    PRICE price = level->get_price();

    // Empty list or new highest price
    if (!buy_list_head || price > buy_list_head->get_price()) {
        level->set_next_level(buy_list_head);
        level->set_prev_level(nullptr);
        if (buy_list_head) buy_list_head->set_prev_level(level);
        buy_list_head = level;
        return;
    }

    // Walk to find insertion point (descending order)
    Level* cur = buy_list_head;
    while (cur->get_next_level() && cur->get_next_level()->get_price() > price) {
        cur = cur->get_next_level();
    }
    // Insert after cur
    level->set_next_level(cur->get_next_level());
    level->set_prev_level(cur);
    if (cur->get_next_level()) cur->get_next_level()->set_prev_level(level);
    cur->set_next_level(level);
}

// Sell list: ascending price order (head = lowest)
void Book::insert_level_sorted_sell(Level* level) {
    PRICE price = level->get_price();

    // Empty list or new lowest price
    if (!sell_list_head || price < sell_list_head->get_price()) {
        level->set_next_level(sell_list_head);
        level->set_prev_level(nullptr);
        if (sell_list_head) sell_list_head->set_prev_level(level);
        sell_list_head = level;
        return;
    }

    // Walk to find insertion point (ascending order)
    Level* cur = sell_list_head;
    while (cur->get_next_level() && cur->get_next_level()->get_price() < price) {
        cur = cur->get_next_level();
    }
    // Insert after cur
    level->set_next_level(cur->get_next_level());
    level->set_prev_level(cur);
    if (cur->get_next_level()) cur->get_next_level()->set_prev_level(level);
    cur->set_next_level(level);
}

void Book::remove_level_from_buy_list(Level* level) {
    Level* prev = level->get_prev_level();
    Level* next = level->get_next_level();
    if (prev) prev->set_next_level(next);
    else buy_list_head = next; // was head
    if (next) next->set_prev_level(prev);
    level->set_prev_level(nullptr);
    level->set_next_level(nullptr);
}

void Book::remove_level_from_sell_list(Level* level) {
    Level* prev = level->get_prev_level();
    Level* next = level->get_next_level();
    if (prev) prev->set_next_level(next);
    else sell_list_head = next; // was head
    if (next) next->set_prev_level(prev);
    level->set_prev_level(nullptr);
    level->set_next_level(nullptr);
}

// --- Core methods ---

const Trades& Book::place_order(
    ID order_id,
    ID agent_id,
    OrderType order_type,
    PRICE price,
    Volume volume
) {
    trade_buffer.clear();

    if (LOB_UNLIKELY(price <= 0 || volume == 0)) {
        return trade_buffer;
    }

    Order* order = order_pool.allocate(
        order_id, agent_id, order_type, price, volume, volume, ACTIVE
    );

    if (order_type == BUY) {
        while (best_ask && price >= best_ask->get_price() && !order->is_fulfilled()) {
            bool level_empty = match_against_level(order, best_ask);
            if (level_empty) {
                PRICE empty_price = best_ask->get_price();
                Level* empty_level = best_ask;
                // Unlink from sorted list BEFORE deallocation
                remove_level_from_sell_list(empty_level);
                sell_side_limits.erase(empty_price);
                level_pool.deallocate(empty_level);
                // best_ask (sell_list_head) already updated by remove_level_from_sell_list
            }
        }
    } else {
        while (best_bid && price <= best_bid->get_price() && !order->is_fulfilled()) {
            bool level_empty = match_against_level(order, best_bid);
            if (level_empty) {
                PRICE empty_price = best_bid->get_price();
                Level* empty_level = best_bid;
                remove_level_from_buy_list(empty_level);
                buy_side_limits.erase(empty_price);
                level_pool.deallocate(empty_level);
            }
        }
    }

    if (!order->is_fulfilled()) {
        insert_resting_order(order);
    } else {
        order_pool.deallocate(order);
    }

    return trade_buffer;
}

bool Book::match_against_level(Order* incoming_order, Level* level) {
    if (LOB_UNLIKELY(!incoming_order || !level || level->is_empty())) {
        return false;
    }

    while (level->get_head() && !incoming_order->is_fulfilled()) {
        Order* resting_order = level->get_head();
        Volume resting_remaining = resting_order->get_remaining_volume();
        Volume incoming_remaining = incoming_order->get_remaining_volume();
        Volume fill_volume = (resting_remaining < incoming_remaining)
                            ? resting_remaining
                            : incoming_remaining;

        resting_order->fill(fill_volume);
        incoming_order->fill(fill_volume);
        level->decrease_volume(fill_volume);

        trade_buffer.emplace_back(
            incoming_order->get_order_id(),
            resting_order->get_order_id(),
            level->get_price(),
            fill_volume
        );

        if (resting_order->is_fulfilled()) {
            resting_order->set_order_status(FULFILLED);
            Order* fulfilled_order = level->pop_front();
            id_to_order.erase(fulfilled_order->get_order_id());
            order_pool.deallocate(fulfilled_order);
        }
    }

    return level->is_empty();
}

void Book::delete_order(ID id) {
    auto it = id_to_order.find(id);
    if (it == id_to_order.end()) {
        return;
    }

    Order* order = it->second;
    if (order->get_order_status() == ACTIVE) {
        bool is_buy = (order->get_order_type() == BUY);
        remove_order_from_level(order, is_buy);
        id_to_order.erase(it);
        order_pool.deallocate(order);
    } else {
        id_to_order.erase(it);
    }
}

void Book::insert_resting_order(Order* order) {
    PRICE price = order->get_order_price();
    bool is_buy = (order->get_order_type() == BUY);

    Level* level = get_or_create_level(price, is_buy);
    level->push_back(order);

    id_to_order[order->get_order_id()] = order;
}

Level* Book::get_or_create_level(PRICE price, bool is_buy) {
    PriceLevelMap& limits = is_buy ? buy_side_limits : sell_side_limits;
    auto it = limits.find(price);

    if (it != limits.end()) {
        return it->second;
    }

    Level* level = level_pool.allocate(price);
    limits[price] = level;

    // Insert into sorted intrusive list
    if (is_buy) {
        insert_level_sorted_buy(level);
    } else {
        insert_level_sorted_sell(level);
    }

    return level;
}

void Book::remove_order_from_level(Order* order, bool is_buy) {
    PRICE price = order->get_order_price();
    PriceLevelMap& limits = is_buy ? buy_side_limits : sell_side_limits;

    auto it = limits.find(price);
    if (it == limits.end()) {
        return;
    }

    Level* level = it->second;
    level->erase(order);
    order->set_order_status(DELETED);

    if (level->is_empty()) {
        // Unlink from sorted list BEFORE deallocation
        if (is_buy) {
            remove_level_from_buy_list(level);
        } else {
            remove_level_from_sell_list(level);
        }
        limits.erase(it);
        level_pool.deallocate(level);
    }
}

PRICE Book::get_best_buy() const {
    return best_bid ? best_bid->get_price() : 0;
}

PRICE Book::get_best_sell() const {
    return best_ask ? best_ask->get_price() : 0;
}

PRICE Book::get_spread() const {
    PRICE bid = get_best_buy();
    PRICE ask = get_best_sell();
    if (bid == 0 || ask == 0) return 0;
    return ask - bid;
}

double Book::get_mid_price() const {
    PRICE bid = get_best_buy();
    PRICE ask = get_best_sell();
    if (bid == 0 || ask == 0) return 0.0;
    return (bid + ask) / 2.0;
}

std::vector<PRICE> Book::get_buy_prices() const {
    std::vector<PRICE> result;
    // Walk intrusive list (already sorted descending)
    for (Level* l = buy_list_head; l; l = l->get_next_level()) {
        if (!l->is_empty()) {
            result.push_back(l->get_price());
        }
    }
    return result;
}

std::vector<PRICE> Book::get_sell_prices() const {
    std::vector<PRICE> result;
    // Walk intrusive list (already sorted ascending)
    for (Level* l = sell_list_head; l; l = l->get_next_level()) {
        if (!l->is_empty()) {
            result.push_back(l->get_price());
        }
    }
    return result;
}

OrderStatus Book::get_order_status(ID id) const {
    auto it = id_to_order.find(id);
    if (it != id_to_order.end()) {
        return it->second->get_order_status();
    }
    return DELETED;
}

void Book::print() const {
    std::cout << "==== BUY SIDE ====" << std::endl;
    std::cout << "Best Buy: " << get_best_buy() << std::endl;
    for (Level* l = buy_list_head; l; l = l->get_next_level()) {
        l->print();
    }
    std::cout << "==== SELL SIDE ====" << std::endl;
    std::cout << "Best Sell: " << get_best_sell() << std::endl;
    for (Level* l = sell_list_head; l; l = l->get_next_level()) {
        l->print();
    }
}
