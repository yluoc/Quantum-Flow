// Normalize OKX WebSocket messages to NormalizedEvent (mirrors normalizer.py).
#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "event.hpp"

namespace quantumflow::pipeline {

/// Normalize a decoded OKX WebSocket message into zero or more NormalizedEvents.
std::vector<NormalizedEvent> normalize_okx(
    int64_t ts_recv_epoch_ms,
    int64_t ts_recv_mono_ns,
    int64_t ts_decoded_mono_ns,
    const nlohmann::json& msg);

} // namespace quantumflow::pipeline
