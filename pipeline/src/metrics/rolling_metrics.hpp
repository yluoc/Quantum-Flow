// Rolling metrics aggregator for latency tracking (mirrors metrics/rolling.py).
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "../event.hpp"

namespace quantumflow::pipeline {

/// Rolling latency metrics with percentile printing and CSV export.
/// update() runs on the WS thread; print_stats()/export_csv() run on helper
/// threads, so all state is guarded by a mutex.
class RollingMetrics {
public:
    explicit RollingMetrics(double window_seconds = 5.0);

    void update(const NormalizedEvent& event);

    /// Print p50/p95/p99 latencies if >=1s elapsed (or force).
    void print_stats(bool force = false);

    /// Export per-(symbol,channel) mean/std/min/max stats to a CSV file.
    void export_csv(const std::string& path);

private:
    using Key = std::pair<std::string, std::string>;  // (symbol, channel)
    using Sample = std::pair<int64_t, double>;        // (t_mono_ms, value)

    double window_seconds_;
    int64_t window_ms_;

    std::mutex mu_;

    std::deque<Sample> latency_exchange_to_recv_;
    std::deque<Sample> latency_recv_to_decode_;
    std::deque<Sample> latency_decode_to_proc_;

    std::map<Key, std::deque<Sample>> latency_by_key_;
    std::map<Key, std::deque<Sample>> staleness_by_key_;
    std::map<Key, int64_t> last_ts_exchange_;

    std::map<std::string, int64_t> message_counts_;

    int64_t count_zero_recv_to_decode_ = 0;
    int64_t count_zero_decode_to_proc_ = 0;
    int64_t total_events_ = 0;

    int64_t last_print_mono_ms_;
};

} // namespace quantumflow::pipeline
