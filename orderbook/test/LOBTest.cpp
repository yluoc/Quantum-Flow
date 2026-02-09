#include <gtest/gtest.h>
#include <algorithm>
#include "LOB/Book.h"
#include "LOB/Order.h"
#include "LOB/Level.h"

// Order Tests
TEST(order_test, fill_order_beyond_volume) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(30);
	EXPECT_EQ(order.get_remaining_volume(), 20);
}

TEST(order_test, order_status_after_partial_fill) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(20);
	EXPECT_EQ(order.get_order_status(), ACTIVE);
	EXPECT_EQ(order.get_remaining_volume(), 30);
}

TEST(order_test, order_status_after_full_fill) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(50);
	EXPECT_EQ(order.get_order_status(), FULFILLED);
	EXPECT_EQ(order.get_remaining_volume(), 0);
}

TEST(order_test, set_order_status) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.set_order_status(DELETED);
	EXPECT_EQ(order.get_order_status(), DELETED);
}

TEST(order_test, order_initial_state) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	EXPECT_EQ(order.get_order_id(), 1);
	EXPECT_EQ(order.get_agent_id(), 1);
	EXPECT_EQ(order.get_order_type(), BUY);
	EXPECT_EQ(order.get_order_price(), 100);
	EXPECT_EQ(order.get_remaining_volume(), 50);
	EXPECT_EQ(order.get_order_status(), ACTIVE);
}

// Level Tests
TEST(level_test, insert_multiple_orders) {
	Level level(100);
	
	Order order1(1, 1, BUY, 100, 50, 50, ACTIVE);
	Order order2(2, 1, BUY, 100, 30, 30, ACTIVE);
	Order order3(3, 1, BUY, 100, 20, 20, ACTIVE);
	
	level.push_back(&order1);
	level.push_back(&order2);
	level.push_back(&order3);
	
	EXPECT_EQ(level.get_order_number(), 3);
	EXPECT_EQ(level.get_total_volume(), 100);
}

TEST(level_test, delete_order_from_level) {
	Level level(100);
	
	Order order1(1, 1, BUY, 100, 50, 50, ACTIVE);
	Order order2(2, 1, BUY, 100, 30, 30, ACTIVE);
	Order order3(3, 1, BUY, 100, 20, 20, ACTIVE);
	
	level.push_back(&order1);
	level.push_back(&order2);
	level.push_back(&order3);
	
	level.erase(&order2);
	
	EXPECT_EQ(level.get_order_number(), 2);
	EXPECT_EQ(level.get_total_volume(), 70);
}

TEST(level_test, match_order_partial_fill) {
	Level level(100);
	
	Order buy_order(1, 1, BUY, 100, 50, 50, ACTIVE);
	Order sell_order(2, 2, SELL, 100, 30, 30, ACTIVE);
	
	level.push_back(&sell_order);
	
	Trades trades;
	Order* resting = level.get_head();
	if (resting) {
		Volume fill_volume = std::min(resting->get_remaining_volume(), buy_order.get_remaining_volume());
		resting->fill(fill_volume);
		buy_order.fill(fill_volume);
		level.decrease_volume(fill_volume);
		trades.emplace_back(buy_order.get_order_id(), resting->get_order_id(), level.get_price(), fill_volume);
		if (resting->is_fulfilled()) {
			level.pop_front();
		}
	}
	
	EXPECT_EQ(trades.size(), 1);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	EXPECT_EQ(buy_order.get_remaining_volume(), 20);
	EXPECT_EQ(sell_order.get_remaining_volume(), 0);
}

// Book Tests
TEST(book_test, place_buy_order_no_match) {
	Book book;
	const Trades& trades = book.place_order(1, 1, BUY, 100, 50);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 1);
	EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(book_test, place_sell_order_no_match) {
	Book book;
	const Trades& trades = book.place_order(1, 1, SELL, 100, 50);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_sell_levels_count(), 1);
	EXPECT_EQ(book.get_best_sell(), 100);
}

TEST(book_test, place_buy_order_with_match) {
	Book book;
	book.place_order(1, 1, SELL, 100, 30);
	
	const Trades& trades = book.place_order(2, 2, BUY, 100, 50);
	
	EXPECT_EQ(trades.size(), 1);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	
	EXPECT_EQ(book.get_sell_levels_count(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(book_test, place_sell_order_with_match) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	const Trades& trades = book.place_order(2, 2, SELL, 100, 50);
	
	EXPECT_EQ(trades.size(), 1);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	
	EXPECT_EQ(book.get_buy_levels_count(), 0);
	EXPECT_EQ(book.get_sell_levels_count(), 1);
}

TEST(book_test, multiple_orders_same_price) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	book.place_order(2, 1, BUY, 100, 20);
	
	const Trades& trades = book.place_order(3, 2, SELL, 100, 40);
	
	EXPECT_EQ(trades.size(), 2);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	EXPECT_EQ(trades[1].get_trade_volume(), 10);
	
	EXPECT_EQ(book.get_buy_levels_count(), 1);
	EXPECT_EQ(book.get_sell_levels_count(), 0);
}

TEST(book_test, delete_order) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	book.delete_order(1);
	
	EXPECT_EQ(book.get_buy_levels_count(), 0);
}

TEST(book_test, delete_order_not_in_book) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	book.delete_order(2);
	
	EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(book_test, place_order_with_invalid_price) {
	Book book;
	const Trades& trades = book.place_order(1, 1, BUY, 0, 30);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 0);
}

TEST(book_test, fifo_at_same_price) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 100, 20);
    book.place_order(3, 1, BUY, 100, 30);
    
    const Trades& trades = book.place_order(4, 2, SELL, 100, 60);
    
    EXPECT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].get_matched_order(), 1);
    EXPECT_EQ(trades[1].get_matched_order(), 2);
    EXPECT_EQ(trades[2].get_matched_order(), 3);
    EXPECT_EQ(trades[0].get_trade_volume(), 10);
    EXPECT_EQ(trades[1].get_trade_volume(), 20);
    EXPECT_EQ(trades[2].get_trade_volume(), 30);
}

TEST(book_test, partial_fill_multiple_orders) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 100, 20);
    
    const Trades& trades = book.place_order(3, 2, SELL, 100, 25);
    
    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].get_trade_volume(), 10);
    EXPECT_EQ(trades[1].get_trade_volume(), 15);
    
    EXPECT_EQ(book.get_order_status(2), ACTIVE);
    EXPECT_EQ(book.get_order_status(1), DELETED);
}

TEST(book_test, cancel_resting_order) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 100, 20);
    
    book.delete_order(1);
    
    EXPECT_EQ(book.get_buy_levels_count(), 1);
    EXPECT_EQ(book.get_order_status(1), DELETED);
    EXPECT_EQ(book.get_order_status(2), ACTIVE);
    
    const Trades& trades = book.place_order(3, 2, SELL, 100, 20);
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].get_matched_order(), 2);
}

TEST(book_test, cancel_nonexistent_order) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    
    book.delete_order(999);
    
    EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(book_test, best_bid_ask_invariants) {
    Book book;
    
    EXPECT_EQ(book.get_best_buy(), 0);
    EXPECT_EQ(book.get_best_sell(), 0);
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 110);
    
    book.place_order(3, 2, SELL, 120, 10);
    book.place_order(4, 2, SELL, 115, 10);
    
    EXPECT_EQ(book.get_best_sell(), 115);
}

TEST(book_test, best_bid_ask_updates_after_fill) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 110);
    
    book.place_order(3, 2, SELL, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(book_test, best_bid_ask_updates_after_cancel) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 110);
    
    book.delete_order(2);
    
    EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(book_test, spread_calculation) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 110, 10);
    
    EXPECT_EQ(book.get_spread(), 10);
}

TEST(book_test, mid_price_calculation) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 110, 10);
    
    EXPECT_DOUBLE_EQ(book.get_mid_price(), 105.0);
}

TEST(book_test, empty_book_after_all_filled) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 100, 10);
    
    EXPECT_EQ(book.get_buy_levels_count(), 0);
    EXPECT_EQ(book.get_best_buy(), 0);
}

TEST(book_test, cancel_after_partial_fill) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 100);
    
    book.place_order(2, 2, SELL, 100, 30);
    
    EXPECT_EQ(book.get_order_status(1), ACTIVE);
    EXPECT_EQ(book.get_resting_orders_count(), 1);
    
    book.delete_order(1);
    
    EXPECT_EQ(book.get_order_status(1), DELETED);
    EXPECT_EQ(book.get_resting_orders_count(), 0);
    EXPECT_EQ(book.get_best_buy(), 0);
}

TEST(book_test, fulfilled_orders_removed_from_index) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 100, 20);
    
    EXPECT_EQ(book.get_resting_orders_count(), 2);
    
    book.place_order(3, 2, SELL, 100, 15);
    
    EXPECT_EQ(book.get_resting_orders_count(), 1);
    EXPECT_EQ(book.get_order_status(1), DELETED);
    EXPECT_EQ(book.get_order_status(2), ACTIVE);
}

TEST(book_test, pool_reuse_no_memory_growth) {
    Book book(1000);
    
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (ID i = 1; i <= 100; ++i) {
            book.place_order(cycle * 1000 + i, 1, BUY, 100 + (i % 10), 10);
        }
        
        for (ID i = 1; i <= 100; ++i) {
            book.place_order(cycle * 10000 + i, 2, SELL, 100, 1000);
        }
        
        for (ID i = 1; i <= 100; ++i) {
            book.delete_order(cycle * 1000 + i);
        }
        for (ID i = 1; i <= 100; ++i) {
            book.delete_order(cycle * 10000 + i);
        }
    }
    
    EXPECT_EQ(book.get_resting_orders_count(), 0);
}

// Main function
int main(int argc, char **argv) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
