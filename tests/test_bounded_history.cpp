#include <gtest/gtest.h>
#include "common/bounded_history.hpp"

using namespace quantumflow;

TEST(BoundedHistory, PushAccumulatesInOrder) {
    BoundedHistory<int, 4, 2> h;
    h.push(1);
    h.push(2);
    h.push(3);
    EXPECT_EQ(h.size(), 3u);
    EXPECT_EQ(h.view(), (std::vector<int>{1, 2, 3}));
}

TEST(BoundedHistory, TrimIsNoOpAtOrBelowSoftMax) {
    BoundedHistory<int, 4, 2> h;
    h.push(1);
    h.push(2);
    h.push(3);
    h.push(4); // size == SoftMax, not greater
    h.trim();
    EXPECT_EQ(h.size(), 4u);
    EXPECT_EQ(h.view(), (std::vector<int>{1, 2, 3, 4}));
}

TEST(BoundedHistory, TrimKeepsMostRecentOncePastSoftMax) {
    BoundedHistory<int, 4, 2> h;
    for (int i = 1; i <= 5; ++i) h.push(i); // size 5 > SoftMax 4
    h.trim();
    EXPECT_EQ(h.size(), 2u);
    EXPECT_EQ(h.view(), (std::vector<int>{4, 5})); // last Keep=2
}

TEST(BoundedHistory, RepeatedPushTrimStaysBounded) {
    BoundedHistory<int, 4, 2> h;
    int last = 0;
    for (int i = 1; i <= 100; ++i) {
        h.push(i);
        h.trim();
        last = i;
        EXPECT_LE(h.size(), 4u);
    }
    // Most recent element is always retained.
    EXPECT_EQ(h.view().back(), last);
}

TEST(BoundedHistory, ClearEmpties) {
    BoundedHistory<int, 4, 2> h;
    h.push(1);
    h.push(2);
    h.clear();
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.size(), 0u);
}
