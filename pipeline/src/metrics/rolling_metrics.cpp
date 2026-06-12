#include "rolling_metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "../time_helpers.hpp"

// UTF-8 right-arrow (U+2192) as its own literal — see stdout_sink.cpp.
#define AR "\xE2\x86\x92"

namespace quantumflow::pipeline {

namespace {

void evict(std::deque<std::pair<int64_t, double>>& dq, int64_t cutoff_ms) {
    while (!dq.empty() && dq.front().first < cutoff_ms) dq.pop_front();
}

// Percentile from an unsorted value list using Python's index convention:
// idx = int((p/100) * (n - 1)).
double percentile(std::vector<double>& sorted_vals, double p) {
    if (sorted_vals.empty()) return 0.0;
    const int n = static_cast<int>(sorted_vals.size());
    int idx = static_cast<int>((p / 100.0) * (n - 1));
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return sorted_vals[idx];
}

std::string utc_iso8601_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t secs = system_clock::to_time_t(now);
    const auto us = duration_cast<microseconds>(now.time_since_epoch()).count() % 1000000;
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    char base[32];
    std::strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    char out[48];
    std::snprintf(out, sizeof(out), "%s.%06lld+00:00", base, static_cast<long long>(us));
    return out;
}

struct Stats {
    int count = 0;
    double mean = 0.0, std = 0.0, min = 0.0, max = 0.0;
};

Stats compute_stats(const std::deque<std::pair<int64_t, double>>& dq) {
    Stats s;
    s.count = static_cast<int>(dq.size());
    if (s.count == 0) return s;
    double sum = 0.0;
    s.min = dq.front().second;
    s.max = dq.front().second;
    for (const auto& [t, v] : dq) {
        sum += v;
        s.min = std::min(s.min, v);
        s.max = std::max(s.max, v);
    }
    s.mean = sum / s.count;
    if (s.count > 1) {
        double sq = 0.0;
        for (const auto& [t, v] : dq) sq += (v - s.mean) * (v - s.mean);
        s.std = std::sqrt(sq / (s.count - 1));
    }
    return s;
}

} // namespace

RollingMetrics::RollingMetrics(double window_seconds)
    : window_seconds_(window_seconds),
      window_ms_(static_cast<int64_t>(window_seconds * 1000)),
      last_print_mono_ms_(now_mono_ms()) {}

void RollingMetrics::update(const NormalizedEvent& event) {
    const int64_t t_mono_ms = now_mono_ms();

    const double lat_ex_to_recv_ms =
        static_cast<double>(event.ts_recv_epoch_ms - event.ts_exchange_ms);
    const int64_t lat_recv_to_decode_ns = event.ts_decoded_mono_ns - event.ts_recv_mono_ns;
    const int64_t lat_decode_to_proc_ns = event.ts_proc_mono_ns - event.ts_decoded_mono_ns;

    std::lock_guard<std::mutex> lk(mu_);

    ++total_events_;
    if (lat_recv_to_decode_ns == 0) ++count_zero_recv_to_decode_;
    if (lat_decode_to_proc_ns == 0) ++count_zero_decode_to_proc_;

    latency_exchange_to_recv_.emplace_back(t_mono_ms, lat_ex_to_recv_ms);
    latency_recv_to_decode_.emplace_back(t_mono_ms, static_cast<double>(lat_recv_to_decode_ns));
    latency_decode_to_proc_.emplace_back(t_mono_ms, static_cast<double>(lat_decode_to_proc_ns));

    const int64_t cutoff_ms = t_mono_ms - window_ms_;
    evict(latency_exchange_to_recv_, cutoff_ms);
    evict(latency_recv_to_decode_, cutoff_ms);
    evict(latency_decode_to_proc_, cutoff_ms);

    const Key key{event.symbol, event.channel};

    auto& lat_dq = latency_by_key_[key];
    lat_dq.emplace_back(t_mono_ms, lat_ex_to_recv_ms);
    evict(lat_dq, cutoff_ms);

    auto last_it = last_ts_exchange_.find(key);
    if (last_it != last_ts_exchange_.end()) {
        const double stale_ms = static_cast<double>(event.ts_exchange_ms - last_it->second);
        auto& stale_dq = staleness_by_key_[key];
        stale_dq.emplace_back(t_mono_ms, stale_ms);
        evict(stale_dq, cutoff_ms);
    }
    last_ts_exchange_[key] = event.ts_exchange_ms;

    ++message_counts_[event.symbol];
}

void RollingMetrics::print_stats(bool force) {
    std::lock_guard<std::mutex> lk(mu_);

    const int64_t now = now_mono_ms();
    if (!force && (now - last_print_mono_ms_) < 1000) return;
    last_print_mono_ms_ = now;

    std::vector<double> ex_to_recv, recv_to_decode, decode_to_proc;
    ex_to_recv.reserve(latency_exchange_to_recv_.size());
    for (const auto& [t, v] : latency_exchange_to_recv_) ex_to_recv.push_back(v);
    recv_to_decode.reserve(latency_recv_to_decode_.size());
    for (const auto& [t, v] : latency_recv_to_decode_) recv_to_decode.push_back(v);
    decode_to_proc.reserve(latency_decode_to_proc_.size());
    for (const auto& [t, v] : latency_decode_to_proc_) decode_to_proc.push_back(v);

    constexpr int kMinSamples = 20;

    std::ostringstream msgs;
    bool first = true;
    for (const auto& [sym, cnt] : message_counts_) {  // std::map is sorted by symbol
        if (!first) msgs << ", ";
        msgs << sym << ":" << cnt;
        first = false;
    }

    std::vector<std::string> parts;
    char buf[256];

    if (static_cast<int>(ex_to_recv.size()) >= kMinSamples) {
        std::sort(ex_to_recv.begin(), ex_to_recv.end());
        std::snprintf(buf, sizeof(buf),
                      "Ex" AR "Recv p50=%.1fms p95=%.1fms p99=%.1fms",
                      percentile(ex_to_recv, 50), percentile(ex_to_recv, 95),
                      percentile(ex_to_recv, 99));
        parts.emplace_back(buf);
    }

    if (static_cast<int>(recv_to_decode.size()) >= kMinSamples) {
        std::sort(recv_to_decode.begin(), recv_to_decode.end());
        const double zero_rate =
            (static_cast<double>(count_zero_recv_to_decode_) / std::max<int64_t>(1, total_events_)) * 100.0;
        std::snprintf(buf, sizeof(buf),
                      "Recv" AR "Decode p50=%.3fus p95=%.3fus p99=%.3fus (zero=%.1f%%)",
                      percentile(recv_to_decode, 50) / 1000.0,
                      percentile(recv_to_decode, 95) / 1000.0,
                      percentile(recv_to_decode, 99) / 1000.0, zero_rate);
        parts.emplace_back(buf);
    }

    if (static_cast<int>(decode_to_proc.size()) >= kMinSamples) {
        std::sort(decode_to_proc.begin(), decode_to_proc.end());
        const double zero_rate =
            (static_cast<double>(count_zero_decode_to_proc_) / std::max<int64_t>(1, total_events_)) * 100.0;
        std::snprintf(buf, sizeof(buf),
                      "Decode" AR "Proc p50=%.3fus p95=%.3fus p99=%.3fus (zero=%.1f%%)",
                      percentile(decode_to_proc, 50) / 1000.0,
                      percentile(decode_to_proc, 95) / 1000.0,
                      percentile(decode_to_proc, 99) / 1000.0, zero_rate);
        parts.emplace_back(buf);
    }

    if (!parts.empty()) {
        std::ostringstream line;
        line << "Metrics | ";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) line << " | ";
            line << parts[i];
        }
        line << " | Msgs: " << msgs.str();
        std::printf("%s\n", line.str().c_str());
        std::fflush(stdout);
    }
}

void RollingMetrics::export_csv(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);

    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "[metrics] failed to open %s\n", path.c_str());
        return;
    }

    const std::string gen = utc_iso8601_now();

    // Union of keys present in either map, sorted (std::map iterates sorted).
    std::vector<Key> keys;
    for (const auto& [k, v] : latency_by_key_) keys.push_back(k);
    for (const auto& [k, v] : staleness_by_key_) {
        if (latency_by_key_.find(k) == latency_by_key_.end()) keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());

    f << "generated_at_utc,symbol,channel,lat_count,lat_mean_ms,lat_std_ms,lat_min_ms,"
         "lat_max_ms,stale_count,stale_mean_ms,stale_std_ms,stale_min_ms,stale_max_ms\n";

    char num[32];
    auto fmt = [&num](double v) {
        std::snprintf(num, sizeof(num), "%.3f", v);
        return std::string(num);
    };

    for (const auto& key : keys) {
        Stats lat;
        if (auto it = latency_by_key_.find(key); it != latency_by_key_.end())
            lat = compute_stats(it->second);
        Stats stale;
        if (auto it = staleness_by_key_.find(key); it != staleness_by_key_.end())
            stale = compute_stats(it->second);

        f << gen << ',' << key.first << ',' << key.second << ','
          << lat.count << ',' << fmt(lat.mean) << ',' << fmt(lat.std) << ',' << fmt(lat.min)
          << ',' << fmt(lat.max) << ','
          << stale.count << ',' << fmt(stale.mean) << ',' << fmt(stale.std) << ','
          << fmt(stale.min) << ',' << fmt(stale.max) << '\n';
    }
}

} // namespace quantumflow::pipeline
