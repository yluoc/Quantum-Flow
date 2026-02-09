#pragma once

#include <atomic>
#include "common/market_data_packet.hpp"
#include "memory/allocator.h"

namespace quantumflow {

/// Thread-safe bridge wrapping the lock-free ring buffer for Python→C++ market data transfer.
/// Uses SPSC (single producer, single consumer) pattern:
///   Producer: Python pipeline (via PyBind11)
///   Consumer: C++ main loop
class MarketDataBridge {
public:
    static constexpr size_t CAPACITY = 4096;

    bool push(const MarketDataPacket& packet) {
        bool ok = ring_.tryPush(packet);
        if (ok) {
            push_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            drop_count_.fetch_add(1, std::memory_order_relaxed);
        }
        return ok;
    }

    bool pop(MarketDataPacket& packet) {
        bool ok = ring_.tryPop(packet);
        if (ok) {
            pop_count_.fetch_add(1, std::memory_order_relaxed);
        }
        return ok;
    }

    uint64_t push_count() const { return push_count_.load(std::memory_order_relaxed); }
    uint64_t pop_count()  const { return pop_count_.load(std::memory_order_relaxed); }
    uint64_t drop_count() const { return drop_count_.load(std::memory_order_relaxed); }

    bool empty() const { return ring_.empty(); }
    size_t size() const { return ring_.size(); }

private:
    engine::memory::fast::LockFreeRingBuffer<MarketDataPacket, CAPACITY> ring_;
    std::atomic<uint64_t> push_count_{0};
    std::atomic<uint64_t> pop_count_{0};
    std::atomic<uint64_t> drop_count_{0};
};

/// Global bridge instance shared between PyBind11 module and C++ main loop.
inline MarketDataBridge& global_bridge() {
    static MarketDataBridge instance;
    return instance;
}

} // namespace quantumflow
