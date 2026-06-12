// Base sink interface (mirrors pipeline/src/sinks/base.py).
#pragma once

#include "../event.hpp"

namespace quantumflow::pipeline {

/// Interface for event sinks. write() is called from the WS receive thread
/// (single writer); close() is called once at shutdown from the main thread.
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const NormalizedEvent& event) = 0;
    virtual void close() = 0;
};

} // namespace quantumflow::pipeline
