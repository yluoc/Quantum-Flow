// JSONL sink - writes normalized events to partitioned files (mirrors sinks/jsonl.py).
#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "sink.hpp"

namespace quantumflow::pipeline {

/// Writes normalized events to data/okx/{channel}/{YYYY-MM-DD}/{symbol}.jsonl,
/// buffered and flushed by count or elapsed time.
class JsonlSink : public Sink {
public:
    JsonlSink(std::string root = "data",
              double flush_interval_sec = 1.0,
              int flush_count = 100);

    void write(const NormalizedEvent& event) override;
    void close() override;

private:
    void flush();

    std::string root_;
    double flush_interval_sec_;
    int flush_count_;

    std::map<std::string, std::vector<std::string>> buffer_;
    int buffer_count_ = 0;
    std::chrono::steady_clock::time_point last_flush_;
};

} // namespace quantumflow::pipeline
