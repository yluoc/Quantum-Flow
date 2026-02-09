#pragma once

#include <cstdint>

namespace quantumflow {

struct MarketDataPacket {
    char symbol[16];
    uint8_t side;        // 0 = buy, 1 = sell
    uint8_t event_type;  // 0 = book_level, 1 = trade
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;
    uint64_t order_id;
};

} // namespace quantumflow
