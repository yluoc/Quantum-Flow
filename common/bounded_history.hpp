#pragma once

#include <cstddef>
#include <vector>

namespace quantumflow {

/// Append-only history with a bounded footprint, backed by a contiguous vector.
///
/// Trades/events are pushed in arrival order. The buffer is allowed to grow up
/// to `SoftMax` entries; calling trim() once it exceeds that high-water mark
/// drops the oldest entries, retaining the most recent `Keep`. Trimming is an
/// explicit step (not done on push) so callers control exactly when it happens
/// relative to reads — some consumers read the full accumulated window before
/// trimming, others trim before reading.
///
/// Storage stays contiguous so view() can be handed directly to APIs taking
/// `const std::vector<T>&` with no copy. The amortized cost of trimming is O(1)
/// per pushed element (one shift of `Keep` items per `SoftMax - Keep` pushes).
template <typename T, std::size_t SoftMax, std::size_t Keep>
class BoundedHistory {
    static_assert(Keep <= SoftMax, "Keep must not exceed SoftMax");

public:
    void push(const T& value) { data_.push_back(value); }

    /// Drop the oldest entries if the buffer has grown past SoftMax, keeping the
    /// most recent Keep. No-op while at or below the high-water mark.
    void trim() {
        if (data_.size() > SoftMax) {
            data_.erase(data_.begin(),
                        data_.end() - static_cast<std::ptrdiff_t>(Keep));
        }
    }

    /// Contiguous view of the current contents, in arrival order.
    const std::vector<T>& view() const { return data_; }

    std::size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }

private:
    std::vector<T> data_;
};

} // namespace quantumflow
