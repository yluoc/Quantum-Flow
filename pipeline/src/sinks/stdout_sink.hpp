// Stdout sink - prints compact one-liners (mirrors sinks/stdout.py).
#pragma once

#include "sink.hpp"

namespace quantumflow::pipeline {

class StdoutSink : public Sink {
public:
    void write(const NormalizedEvent& event) override;
    void close() override {}
};

} // namespace quantumflow::pipeline
