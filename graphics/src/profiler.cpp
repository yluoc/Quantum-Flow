#include "profiler/profiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>

// ═══════════════════════════════════════════════
// Singleton
// ═══════════════════════════════════════════════
engine::profiler::Profiler& engine::profiler::Profiler::instance() {
    static Profiler inst;
    return inst;
}

// ═══════════════════════════════════════════════
// Begin / End
// ═══════════════════════════════════════════════
void engine::profiler::Profiler::beginSection(const std::string& name) {
    if (!m_enabled) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeTimers[name] = Timer::now();
}

void engine::profiler::Profiler::endSection(const std::string& name) {
    if (!m_enabled) return;
    auto end = Timer::now();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_activeTimers.find(name);
    if (it == m_activeTimers.end()) return;

    double ms = Timer::elapsedMs(it->second, end);
    m_activeTimers.erase(it);

    m_stats[name].name = name;
    m_stats[name].record(ms);
}

// ═══════════════════════════════════════════════
// Frame End
// ═══════════════════════════════════════════════
void engine::profiler::Profiler::frameEnd() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [name, stats] : m_stats) {
        stats.frameEnd();
    }
}

// ═══════════════════════════════════════════════
// Query
// ═══════════════════════════════════════════════
const engine::profiler::SectionStats*
engine::profiler::Profiler::getStats(const std::string& name) const {
    auto it = m_stats.find(name);
    return (it != m_stats.end()) ? &it->second : nullptr;
}

// ═══════════════════════════════════════════════
// Report
// ═══════════════════════════════════════════════
void engine::profiler::Profiler::printReport() const {
    // Collect and sort by name
    std::vector<std::pair<std::string, SectionStats>> sorted(m_stats.begin(), m_stats.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::cout << "\n╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      PROFILER REPORT                            ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ " << std::left << std::setw(28) << "Section"
              << std::right << std::setw(10) << "Last ms"
              << std::setw(10) << "Min ms"
              << std::setw(10) << "Max ms"
              << std::setw(10) << "Avg ms"
              << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";

    for (auto& [name, s] : sorted) {
        std::cout << "║ " << std::left  << std::setw(28) << name
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(10) << s.lastMs
                  << std::setw(10) << (s.minMs < 1e17 ? s.minMs : 0.0)
                  << std::setw(10) << s.maxMs
                  << std::setw(10) << s.avgMs
                  << " ║\n";
    }

    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
}
