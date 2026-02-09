#pragma once

#include <cstdint>

namespace quantumflow {

struct LatencySnapshot {
    double python_to_cpp_us;
    double order_match_us;
    double strategy_eval_us;
    double ws_broadcast_us;
    double total_us;
};

} // namespace quantumflow
