#include <gtest/gtest.h>
#include "common/price_converter.hpp"

using namespace quantumflow;

TEST(PriceConverter, RoundTripCents) {
    PriceConverter pc(100.0);
    double prices[] = {43105.50, 0.01, 99999.99, 1.00};
    for (double p : prices) {
        PRICE internal = pc.to_internal(p);
        double back = pc.to_external(internal);
        EXPECT_DOUBLE_EQ(back, p);
    }
}

TEST(PriceConverter, RoundTripHighPrecision) {
    // For small-value assets (e.g., SHIB at $0.00001234), scale 1e8 fits in uint32_t
    PriceConverter pc(1e8);
    double price = 0.00001234;
    PRICE internal = pc.to_internal(price);
    double back = pc.to_external(internal);
    EXPECT_NEAR(back, price, 1e-8);
}

TEST(PriceConverter, BTCCentsScale) {
    // BTC at $43105.50 with scale 100 (cents) → 4310550, fits in uint32_t
    PriceConverter pc(100.0);
    double price = 43105.50;
    PRICE internal = pc.to_internal(price);
    double back = pc.to_external(internal);
    EXPECT_DOUBLE_EQ(back, price);
}

TEST(PriceConverter, ZeroPrice) {
    PriceConverter pc(100.0);
    EXPECT_EQ(pc.to_internal(0.0), 0u);
    EXPECT_DOUBLE_EQ(pc.to_external(0), 0.0);
}

TEST(PriceConverter, MaxRange) {
    // PRICE is uint32_t, max ~4.29 billion
    // With scale 100: max representable price is ~42,949,672.95
    PriceConverter pc(100.0);
    double max_price = 42949672.95;
    PRICE internal = pc.to_internal(max_price);
    double back = pc.to_external(internal);
    EXPECT_NEAR(back, max_price, 0.01);
}

TEST(PriceConverterRegistry, DefaultScale) {
    PriceConverterRegistry reg(100.0);
    const auto& pc = reg.get("UNKNOWN-SYMBOL");
    EXPECT_DOUBLE_EQ(pc.scale_factor(), 100.0);
}

TEST(PriceConverterRegistry, PerSymbolScale) {
    PriceConverterRegistry reg(100.0);
    reg.set_scale("BTC-USDT", 100.0);
    reg.set_scale("ETH-USDT", 100.0);
    reg.set_scale("SHIB-USDT", 1e8); // needs high precision

    EXPECT_DOUBLE_EQ(reg.get("BTC-USDT").scale_factor(), 100.0);
    EXPECT_DOUBLE_EQ(reg.get("SHIB-USDT").scale_factor(), 1e8);
    EXPECT_DOUBLE_EQ(reg.get("NONEXISTENT").scale_factor(), 100.0);
}
