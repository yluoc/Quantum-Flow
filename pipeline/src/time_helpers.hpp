// Time helpers with explicit clock domain separation.
//
// Mirrors pipeline/src/time_helpers.py:
//   - epoch_ms: wall clock (system_clock), for exchange->recv latency
//   - mono_ns / mono_ms: monotonic (steady_clock), for internal stage latency
#pragma once

#include <chrono>
#include <cstdint>

namespace quantumflow::pipeline {

/// Current epoch time in milliseconds (wall clock, for exchange->recv latency).
inline int64_t now_epoch_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/// Current monotonic time in nanoseconds (high-precision internal stage latency).
inline int64_t now_mono_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

/// Current monotonic time in milliseconds (rolling window bookkeeping).
inline int64_t now_mono_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace quantumflow::pipeline
