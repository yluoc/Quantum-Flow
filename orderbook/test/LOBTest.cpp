#include <gtest/gtest.h>
#include <algorithm>
#include "LOB/Book.h"
#include "LOB/Order.h"
#include "LOB/Level.h"

// Order Tests
TEST(OrderTest, FillOrderBeyondVolume) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(30);
	EXPECT_EQ(order.get_remaining_volume(), 20);
}

TEST(OrderTest, OrderStatusAfterPartialFill) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(20);
	EXPECT_EQ(order.get_order_status(), ACTIVE);
	EXPECT_EQ(order.get_remaining_volume(), 30);
}

TEST(OrderTest, OrderStatusAfterFullFill) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.fill(50);
	EXPECT_EQ(order.get_order_status(), FULFILLED);
	EXPECT_EQ(order.get_remaining_volume(), 0);
}

TEST(OrderTest, SetOrderStatus) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	order.set_order_status(DELETED);
	EXPECT_EQ(order.get_order_status(), DELETED);
}

TEST(OrderTest, OrderInitialState) {
	Order order(1, 1, BUY, 100, 50, 50, ACTIVE);
	EXPECT_EQ(order.get_order_id(), 1);
	EXPECT_EQ(order.get_agent_id(), 1);
	EXPECT_EQ(order.get_order_type(), BUY);
	EXPECT_EQ(order.get_order_price(), 100);
	EXPECT_EQ(order.get_remaining_volume(), 50);
	EXPECT_EQ(order.get_order_status(), ACTIVE);
}

// Level Tests
TEST(LevelTest, InsertMultipleOrders) {
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

TEST(LevelTest, DeleteOrderFromLevel) {
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

TEST(LevelTest, MatchOrderPartialFill) {
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
TEST(BookTest, PlaceBuyOrderNoMatch) {
	Book book;
	const Trades& trades = book.place_order(1, 1, BUY, 100, 50);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 1);
	EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(BookTest, PlaceSellOrderNoMatch) {
	Book book;
	const Trades& trades = book.place_order(1, 1, SELL, 100, 50);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_sell_levels_count(), 1);
	EXPECT_EQ(book.get_best_sell(), 100);
}

TEST(BookTest, PlaceBuyOrderWithMatch) {
	Book book;
	book.place_order(1, 1, SELL, 100, 30);
	
	const Trades& trades = book.place_order(2, 2, BUY, 100, 50);
	
	EXPECT_EQ(trades.size(), 1);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	
	EXPECT_EQ(book.get_sell_levels_count(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(BookTest, PlaceSellOrderWithMatch) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	const Trades& trades = book.place_order(2, 2, SELL, 100, 50);
	
	EXPECT_EQ(trades.size(), 1);
	EXPECT_EQ(trades[0].get_trade_volume(), 30);
	
	EXPECT_EQ(book.get_buy_levels_count(), 0);
	EXPECT_EQ(book.get_sell_levels_count(), 1);
}

TEST(BookTest, MultipleOrdersSamePrice) {
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

TEST(BookTest, DeleteOrder) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	book.delete_order(1);
	
	EXPECT_EQ(book.get_buy_levels_count(), 0);
}

TEST(BookTest, DeleteOrderNotInBook) {
	Book book;
	book.place_order(1, 1, BUY, 100, 30);
	
	book.delete_order(2);
	
	EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(BookTest, PlaceOrderWithInvalidPrice) {
	Book book;
	const Trades& trades = book.place_order(1, 1, BUY, 0, 30);
	
	EXPECT_EQ(trades.size(), 0);
	EXPECT_EQ(book.get_buy_levels_count(), 0);
}

TEST(BookTest, FifoAtSamePrice) {
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

TEST(BookTest, PartialFillMultipleOrders) {
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

TEST(BookTest, CancelRestingOrder) {
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

TEST(BookTest, CancelNonexistentOrder) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    
    book.delete_order(999);
    
    EXPECT_EQ(book.get_buy_levels_count(), 1);
}

TEST(BookTest, BestBidAskInvariants) {
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

TEST(BookTest, BestBidAskUpdatesAfterFill) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 110);
    
    book.place_order(3, 2, SELL, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(BookTest, BestBidAskUpdatesAfterCancel) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 110, 10);
    
    EXPECT_EQ(book.get_best_buy(), 110);
    
    book.delete_order(2);
    
    EXPECT_EQ(book.get_best_buy(), 100);
}

TEST(BookTest, SpreadCalculation) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 110, 10);
    
    EXPECT_EQ(book.get_spread(), 10);
}

TEST(BookTest, MidPriceCalculation) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 110, 10);
    
    EXPECT_DOUBLE_EQ(book.get_mid_price(), 105.0);
}

TEST(BookTest, EmptyBookAfterAllFilled) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 2, SELL, 100, 10);
    
    EXPECT_EQ(book.get_buy_levels_count(), 0);
    EXPECT_EQ(book.get_best_buy(), 0);
}

TEST(BookTest, CancelAfterPartialFill) {
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

TEST(BookTest, FulfilledOrdersRemovedFromIndex) {
    Book book;
    
    book.place_order(1, 1, BUY, 100, 10);
    book.place_order(2, 1, BUY, 100, 20);
    
    EXPECT_EQ(book.get_resting_orders_count(), 2);
    
    book.place_order(3, 2, SELL, 100, 15);
    
    EXPECT_EQ(book.get_resting_orders_count(), 1);
    EXPECT_EQ(book.get_order_status(1), DELETED);
    EXPECT_EQ(book.get_order_status(2), ACTIVE);
}

TEST(BookTest, PoolReuseNoMemoryGrowth) {
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
