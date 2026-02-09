#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <mutex>

namespace engine {
namespace profiler {

// ═══════════════════════════════════════════════
// High-Resolution Timer
// ═══════════════════════════════════════════════
class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    static TimePoint now() { return Clock::now(); }

    static double elapsedMs(TimePoint start, TimePoint end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    static double elapsedUs(TimePoint start, TimePoint end) {
        return std::chrono::duration<double, std::micro>(end - start).count();
    }
};


// ═══════════════════════════════════════════════
// Section Stats
// ═══════════════════════════════════════════════
struct SectionStats {
    std::string name;
    double totalMs = 0.0; // Accumulated this frame
    double lastMs = 0.0; // Last completed call duration
    double minMs = 1e18; // All-time min
    double maxMs = 0.0; // All-time max
    double avgMs = 0.0; // Running average
    uint64_t callCount = 0; // Total calls
    uint64_t frameCount = 0; // Number of frames this section has been active

    void record(double ms) {
        totalMs += ms;
        lastMs = ms;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
        callCount++;
        // Exponential moving average (weight 0.1 for new sample)
        if (callCount == 1) avgMs = ms;
        else avgMs = avgMs * 0.9 + ms * 0.1;
    }

    void frameEnd() {
        frameCount++;
        totalMs = 0.0; // Reset per-frame accumulator
    }
};


// ═══════════════════════════════════════════════
// Profiler — singleton
// ═══════════════════════════════════════════════
class Profiler {
public:
    static Profiler& instance();

    // Begin / end a named section (manual use)
    void beginSection(const std::string& name);
    void endSection(const std::string& name);

    // Call at the end of each frame to reset per-frame accumulators
    void frameEnd();

    // Query stats
    const SectionStats* getStats(const std::string& name) const;
    const std::unordered_map<std::string, SectionStats>& allStats() const { return m_stats; }

    // Print a formatted report to stdout
    void printReport() const;

    // Enable / disable profiling
    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled; }

private:
    Profiler() = default;

    std::unordered_map<std::string, SectionStats> m_stats;
    std::unordered_map<std::string, Timer::TimePoint> m_activeTimers;
    std::mutex m_mutex;
    bool m_enabled = true;
};


// ═══════════════════════════════════════════════
// RAII Scoped Marker  — usage:
//     ScopedProfile marker("Frustum Culling");
// ═══════════════════════════════════════════════
class ScopedProfile {
public:
    explicit ScopedProfile(const std::string& name) : m_name(name) {
        Profiler::instance().beginSection(m_name);
    }
    ~ScopedProfile() {
        Profiler::instance().endSection(m_name);
    }
private:
    std::string m_name;
};

// Convenience macro (double indirection needed so __LINE__ expands before token paste)
#define PROFILE_CONCAT_IMPL(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_IMPL(a, b)
#define PROFILE_SECTION(name) engine::profiler::ScopedProfile PROFILE_CONCAT(__prof_, __LINE__)(name)

} // namespace profiler
} // namespace engine
