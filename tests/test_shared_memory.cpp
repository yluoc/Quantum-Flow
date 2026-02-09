#include <gtest/gtest.h>
#include <thread>
#include <cstring>
#include "bridge/shared_memory.hpp"

using namespace quantumflow;

static MarketDataPacket make_packet(const char* sym, double price, uint64_t qty) {
    MarketDataPacket p{};
    std::strncpy(p.symbol, sym, sizeof(p.symbol) - 1);
    p.side = 0;
    p.event_type = 0;
    p.price = price;
    p.quantity = qty;
    p.timestamp_ns = 12345;
    p.order_id = 0;
    return p;
}

TEST(MarketDataBridge, PushPop) {
    MarketDataBridge bridge;
    auto pkt = make_packet("BTC-USDT", 43000.5, 100);
    ASSERT_TRUE(bridge.push(pkt));
    EXPECT_EQ(bridge.push_count(), 1u);
    EXPECT_EQ(bridge.size(), 1u);

    MarketDataPacket out{};
    ASSERT_TRUE(bridge.pop(out));
    EXPECT_EQ(bridge.pop_count(), 1u);
    EXPECT_STREQ(out.symbol, "BTC-USDT");
    EXPECT_DOUBLE_EQ(out.price, 43000.5);
    EXPECT_EQ(out.quantity, 100u);
    EXPECT_TRUE(bridge.empty());
}

TEST(MarketDataBridge, EmptyPop) {
    MarketDataBridge bridge;
    MarketDataPacket out{};
    EXPECT_FALSE(bridge.pop(out));
    EXPECT_EQ(bridge.pop_count(), 0u);
}

TEST(MarketDataBridge, FullBufferDrops) {
    MarketDataBridge bridge;
    // Fill to capacity - 1 (ring buffer leaves one slot empty)
    for (size_t i = 0; i < MarketDataBridge::CAPACITY - 1; ++i) {
        ASSERT_TRUE(bridge.push(make_packet("X", 1.0, i)));
    }
    // Next push should fail
    EXPECT_FALSE(bridge.push(make_packet("X", 1.0, 9999)));
    EXPECT_EQ(bridge.drop_count(), 1u);
}

TEST(MarketDataBridge, SPSCStress) {
    MarketDataBridge bridge;
    constexpr uint64_t N = 100000;

    // Track actual successful pushes separately from bridge counters,
    // since bridge.push() counts drops when buffer is full.
    std::atomic<uint64_t> pushed{0};

    std::thread producer([&] {
        for (uint64_t i = 0; i < N; ++i) {
            auto pkt = make_packet("STRESS", static_cast<double>(i), i);
            // Spin using the raw push — but we accept that the bridge
            // counts drops for failed attempts. Just keep retrying.
            while (true) {
                if (bridge.push(pkt)) {
                    pushed.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                // yield to let consumer drain
            }
        }
    });

    std::thread consumer([&] {
        MarketDataPacket out{};
        uint64_t received = 0;
        while (received < N) {
            if (bridge.pop(out)) {
                received++;
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(pushed.load(), N);
    EXPECT_EQ(bridge.push_count(), N);
    EXPECT_EQ(bridge.pop_count(), N);
    // drop_count may be > 0 due to transient full-buffer retries; that's expected
}
