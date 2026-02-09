#include <gtest/gtest.h>
#include "strategies/strategy_base.hpp"
#include "strategies/strategy_engine.hpp"
#include "strategies/microstructure/order_book_imbalance.hpp"
#include "strategies/microstructure/market_maker.hpp"
#include "strategies/microstructure/vwap_executor.hpp"
#include "strategies/microstructure/liquidity_detector.hpp"
#include "strategies/crypto/funding_arbitrage.hpp"
#include "strategies/crypto/momentum.hpp"
#include "strategies/equities/pairs_trading.hpp"

using namespace quantumflow;

static BookSnapshot make_snapshot(
    std::vector<PriceLevel> bids,
    std::vector<PriceLevel> asks
) {
    BookSnapshot snap;
    snap.symbol = "TEST";
    snap.bids = std::move(bids);
    snap.asks = std::move(asks);
    snap.best_bid = snap.bids.empty() ? 0.0 : snap.bids[0].price;
    snap.best_ask = snap.asks.empty() ? 0.0 : snap.asks[0].price;
    snap.mid_price = (snap.best_bid + snap.best_ask) / 2.0;
    snap.timestamp_ns = 0;
    return snap;
}

// --- OrderBookImbalance ---

TEST(OrderBookImbalance, BuySignalOnBidHeavy) {
    OrderBookImbalance strat(3, 0.3);
    auto snap = make_snapshot(
        {{100.0, 1000, 5}, {99.0, 800, 3}, {98.0, 600, 2}},
        {{101.0, 100, 1}, {102.0, 50, 1}, {103.0, 50, 1}}
    );
    EXPECT_EQ(strat.evaluate(snap, {}), Signal::BUY);
}

TEST(OrderBookImbalance, SellSignalOnAskHeavy) {
    OrderBookImbalance strat(3, 0.3);
    auto snap = make_snapshot(
        {{100.0, 100, 1}, {99.0, 50, 1}, {98.0, 50, 1}},
        {{101.0, 1000, 5}, {102.0, 800, 3}, {103.0, 600, 2}}
    );
    EXPECT_EQ(strat.evaluate(snap, {}), Signal::SELL);
}

TEST(OrderBookImbalance, NeutralOnBalanced) {
    OrderBookImbalance strat(3, 0.3);
    auto snap = make_snapshot(
        {{100.0, 500, 3}, {99.0, 500, 3}},
        {{101.0, 500, 3}, {102.0, 500, 3}}
    );
    EXPECT_EQ(strat.evaluate(snap, {}), Signal::NEUTRAL);
}

// --- MarketMaker ---

TEST(MarketMaker, NeutralOnZeroInventory) {
    MarketMaker mm(10.0, 0.001);
    auto snap = make_snapshot({{100.0, 500, 3}}, {{101.0, 500, 3}});
    EXPECT_EQ(mm.evaluate(snap, {}), Signal::NEUTRAL);
}

TEST(MarketMaker, SellOnLongInventory) {
    MarketMaker mm(10.0, 0.001);
    // Simulate buys to build inventory
    for (int i = 0; i < 6; ++i)
        mm.on_trade({100.0, 1, 0, 0});
    auto snap = make_snapshot({{100.0, 500, 3}}, {{101.0, 500, 3}});
    EXPECT_EQ(mm.evaluate(snap, {}), Signal::SELL);
}

TEST(MarketMaker, QuoteGeneration) {
    MarketMaker mm(10.0, 0.002);
    auto [bid, ask] = mm.generate_quotes(100.0);
    EXPECT_LT(bid, 100.0);
    EXPECT_GT(ask, 100.0);
    EXPECT_NEAR(ask - bid, 0.2, 0.01); // spread = 100 * 0.002 = 0.2
}

// --- VWAPExecutor ---

TEST(VWAPExecutor, BuyWhenBehindSchedule) {
    VWAPExecutor vwap(1000, 3000, {0.33, 0.33, 0.34});
    auto snap = make_snapshot({{100.0, 500, 3}}, {{101.0, 500, 3}});
    // At time 0, should be buying (0 executed, target ~330)
    EXPECT_EQ(vwap.evaluate(snap, {}), Signal::BUY);
}

TEST(VWAPExecutor, NeutralWhenComplete) {
    VWAPExecutor vwap(100, 3000);
    // Simulate completing the order
    vwap.on_trade({100.0, 100, 0, 0});
    auto snap = make_snapshot({{100.0, 500, 3}}, {{101.0, 500, 3}});
    EXPECT_EQ(vwap.evaluate(snap, {}), Signal::NEUTRAL);
}

// --- LiquidityDetector ---

TEST(LiquidityDetector, DetectsIceberg) {
    LiquidityDetector ld(3, 50, 0.1);
    auto snap = make_snapshot(
        {{100.0, 500, 3}},
        {{101.0, 500, 3}}
    );
    // 6 fills at best bid price
    std::vector<TradeInfo> trades;
    for (int i = 0; i < 6; ++i)
        trades.push_back({100.0, 20, 0, 0});
    EXPECT_EQ(ld.evaluate(snap, trades), Signal::BUY);
}

TEST(LiquidityDetector, NeutralOnFewFills) {
    LiquidityDetector ld(5, 100, 0.1);
    auto snap = make_snapshot({{100.0, 500, 3}}, {{101.0, 500, 3}});
    std::vector<TradeInfo> trades = {{100.0, 10, 0, 0}, {100.0, 10, 0, 0}};
    EXPECT_EQ(ld.evaluate(snap, trades), Signal::NEUTRAL);
}

// --- FundingArbitrage ---

TEST(FundingArbitrage, LongSpotShortPerp) {
    FundingArbitrage fa(0.001);
    fa.set_funding_rate(0.005);
    auto snap = make_snapshot({}, {});
    EXPECT_EQ(fa.evaluate(snap, {}), Signal::LONG_SPOT_SHORT_PERP);
}

TEST(FundingArbitrage, ShortSpotLongPerp) {
    FundingArbitrage fa(0.001);
    fa.set_funding_rate(-0.005);
    auto snap = make_snapshot({}, {});
    EXPECT_EQ(fa.evaluate(snap, {}), Signal::SHORT_SPOT_LONG_PERP);
}

TEST(FundingArbitrage, NeutralOnLowFunding) {
    FundingArbitrage fa(0.001);
    fa.set_funding_rate(0.0001);
    auto snap = make_snapshot({}, {});
    EXPECT_EQ(fa.evaluate(snap, {}), Signal::NEUTRAL);
}

// --- Momentum ---

TEST(Momentum, BuyOnUptrend) {
    MomentumStrategy mom(5, 0.02);
    std::vector<TradeInfo> empty;
    for (double p = 100.0; p <= 104.0; p += 1.0) {
        auto snap = make_snapshot(
            {{p - 0.5, 100, 1}},
            {{p + 0.5, 100, 1}}
        );
        snap.mid_price = p;
        mom.evaluate(snap, empty);
    }
    // 4% rise over 5 ticks → should signal BUY
    auto snap = make_snapshot({{103.5, 100, 1}}, {{104.5, 100, 1}});
    snap.mid_price = 104.0;
    EXPECT_EQ(mom.evaluate(snap, empty), Signal::BUY);
}

TEST(Momentum, SellOnDowntrend) {
    MomentumStrategy mom(5, 0.02);
    std::vector<TradeInfo> empty;
    for (double p = 100.0; p >= 96.0; p -= 1.0) {
        auto snap = make_snapshot(
            {{p - 0.5, 100, 1}},
            {{p + 0.5, 100, 1}}
        );
        snap.mid_price = p;
        mom.evaluate(snap, empty);
    }
    auto snap = make_snapshot({{95.5, 100, 1}}, {{96.5, 100, 1}});
    snap.mid_price = 96.0;
    EXPECT_EQ(mom.evaluate(snap, empty), Signal::SELL);
}

// --- PairsTrading ---

TEST(PairsTrading, ShortPairOnHighSpread) {
    PairsTrading pt(1.0, 5, 1.5);
    // Build history with stable spread around 0
    for (int i = 0; i < 4; ++i)
        pt.update_prices(100.0, 100.0);
    // Sudden divergence: spread jumps high
    pt.update_prices(110.0, 100.0);

    auto snap = make_snapshot({}, {});
    EXPECT_EQ(pt.evaluate(snap, {}), Signal::SHORT_PAIR);
}

TEST(PairsTrading, LongPairOnLowSpread) {
    PairsTrading pt(1.0, 5, 1.5);
    for (int i = 0; i < 4; ++i)
        pt.update_prices(100.0, 100.0);
    // Spread drops sharply
    pt.update_prices(90.0, 100.0);

    auto snap = make_snapshot({}, {});
    EXPECT_EQ(pt.evaluate(snap, {}), Signal::LONG_PAIR);
}

// --- StrategyEngine ---

TEST(StrategyEngine, RunsAllStrategies) {
    StrategyEngine engine;
    engine.add_strategy(std::make_unique<OrderBookImbalance>());
    engine.add_strategy(std::make_unique<MomentumStrategy>());
    EXPECT_EQ(engine.strategy_count(), 2u);

    auto snap = make_snapshot(
        {{100.0, 500, 3}},
        {{101.0, 500, 3}}
    );
    auto signals = engine.evaluate(snap, {});
    EXPECT_EQ(signals.size(), 2u);
    EXPECT_EQ(signals[0].strategy_name, "OrderBookImbalance");
    EXPECT_EQ(signals[1].strategy_name, "Momentum");
}
