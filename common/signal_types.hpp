#pragma once

#include <cstdint>
#include <string>

namespace quantumflow {

enum class Signal : uint8_t {
    NEUTRAL,
    BUY,
    SELL,
    LONG_SPOT_SHORT_PERP,
    SHORT_SPOT_LONG_PERP,
    LONG_PAIR,
    SHORT_PAIR
};

inline const char* signal_to_string(Signal s) {
    switch (s) {
        case Signal::NEUTRAL:              return "NEUTRAL";
        case Signal::BUY:                  return "BUY";
        case Signal::SELL:                 return "SELL";
        case Signal::LONG_SPOT_SHORT_PERP: return "LONG_SPOT_SHORT_PERP";
        case Signal::SHORT_SPOT_LONG_PERP: return "SHORT_SPOT_LONG_PERP";
        case Signal::LONG_PAIR:            return "LONG_PAIR";
        case Signal::SHORT_PAIR:           return "SHORT_PAIR";
    }
    return "UNKNOWN";
}

struct StrategySignal {
    std::string strategy_name;
    std::string symbol;
    Signal signal;
    double confidence;
    uint64_t timestamp_ns;
};

} // namespace quantumflow
