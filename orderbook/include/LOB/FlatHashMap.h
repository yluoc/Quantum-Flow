#ifndef LOB_FLAT_HASH_MAP_H
#define LOB_FLAT_HASH_MAP_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <new>
#include "Macros.h"

/**
 * FlatHashMap: Open-addressing hash map optimized for LOB hot paths.
 *
 * - Power-of-2 table size (fast modulo via bitwise AND)
 * - Fibonacci hashing for good key distribution
 * - Linear probing
 * - Tombstone-based deletion
 * - 70% max load factor
 */
template<typename K, typename V>
class FlatHashMap {
public:
    using value_type = std::pair<K, V>;

private:
    enum SlotState : uint8_t { EMPTY = 0, OCCUPIED = 1, TOMBSTONE = 2 };

    struct Slot {
        alignas(value_type) char storage[sizeof(value_type)];
        SlotState state;

        Slot() : state(EMPTY) {}

        value_type& kv() { return *reinterpret_cast<value_type*>(storage); }
        const value_type& kv() const { return *reinterpret_cast<const value_type*>(storage); }
    };

    Slot* slots_;
    size_t capacity_;   // always power of 2
    size_t size_;       // number of OCCUPIED slots
    size_t used_;       // OCCUPIED + TOMBSTONE (for probing load)

    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr double MAX_LOAD = 0.70;

    // Fibonacci hash for uint64_t keys
    static size_t fib_hash(uint64_t key) {
        return static_cast<size_t>(key * UINT64_C(11400714819323198485));
    }

    size_t slot_index(uint64_t key) const {
        return fib_hash(key) & (capacity_ - 1);
    }

    void destroy_all() {
        if (!slots_) return;
        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].state == OCCUPIED) {
                slots_[i].kv().~value_type();
            }
        }
        ::operator delete(slots_);
        slots_ = nullptr;
    }

    Slot* allocate_slots(size_t cap) {
        Slot* s = static_cast<Slot*>(::operator new(sizeof(Slot) * cap));
        for (size_t i = 0; i < cap; ++i) {
            s[i].state = EMPTY;
        }
        return s;
    }

    void grow_and_rehash() {
        size_t new_cap = capacity_ * 2;
        Slot* new_slots = allocate_slots(new_cap);
        size_t mask = new_cap - 1;

        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].state == OCCUPIED) {
                size_t idx = fib_hash(static_cast<uint64_t>(slots_[i].kv().first)) & mask;
                while (new_slots[idx].state == OCCUPIED) {
                    idx = (idx + 1) & mask;
                }
                new (&new_slots[idx].kv()) value_type(std::move(slots_[i].kv()));
                new_slots[idx].state = OCCUPIED;
                slots_[i].kv().~value_type();
            }
        }

        ::operator delete(slots_);
        slots_ = new_slots;
        capacity_ = new_cap;
        used_ = size_; // tombstones cleared
    }

public:
    FlatHashMap()
        : slots_(allocate_slots(MIN_CAPACITY))
        , capacity_(MIN_CAPACITY)
        , size_(0)
        , used_(0)
    {}

    explicit FlatHashMap(size_t initial_capacity)
        : size_(0), used_(0)
    {
        // round up to power of 2
        size_t cap = MIN_CAPACITY;
        while (cap < initial_capacity) cap <<= 1;
        capacity_ = cap;
        slots_ = allocate_slots(capacity_);
    }

    ~FlatHashMap() { destroy_all(); }

    // Non-copyable
    FlatHashMap(const FlatHashMap&) = delete;
    FlatHashMap& operator=(const FlatHashMap&) = delete;

    // Movable
    FlatHashMap(FlatHashMap&& o) noexcept
        : slots_(o.slots_), capacity_(o.capacity_), size_(o.size_), used_(o.used_)
    {
        o.slots_ = nullptr;
        o.capacity_ = 0;
        o.size_ = 0;
        o.used_ = 0;
    }

    FlatHashMap& operator=(FlatHashMap&& o) noexcept {
        if (this != &o) {
            destroy_all();
            slots_ = o.slots_;
            capacity_ = o.capacity_;
            size_ = o.size_;
            used_ = o.used_;
            o.slots_ = nullptr;
            o.capacity_ = 0;
            o.size_ = 0;
            o.used_ = 0;
        }
        return *this;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void reserve(size_t n) {
        size_t required = static_cast<size_t>(n / MAX_LOAD) + 1;
        if (required <= capacity_) return;
        size_t new_cap = capacity_;
        while (new_cap < required) new_cap <<= 1;

        Slot* new_slots = allocate_slots(new_cap);
        size_t mask = new_cap - 1;

        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].state == OCCUPIED) {
                size_t idx = fib_hash(static_cast<uint64_t>(slots_[i].kv().first)) & mask;
                while (new_slots[idx].state == OCCUPIED) {
                    idx = (idx + 1) & mask;
                }
                new (&new_slots[idx].kv()) value_type(std::move(slots_[i].kv()));
                new_slots[idx].state = OCCUPIED;
                slots_[i].kv().~value_type();
            }
        }

        ::operator delete(slots_);
        slots_ = new_slots;
        capacity_ = new_cap;
        used_ = size_;
    }

    // Iterator
    class iterator {
        friend class FlatHashMap;
        Slot* slots_;
        size_t index_;
        size_t capacity_;

        void advance() {
            while (index_ < capacity_ && slots_[index_].state != OCCUPIED) {
                ++index_;
            }
        }
    public:
        iterator(Slot* s, size_t i, size_t c) : slots_(s), index_(i), capacity_(c) { advance(); }

        value_type& operator*() { return slots_[index_].kv(); }
        value_type* operator->() { return &slots_[index_].kv(); }

        iterator& operator++() {
            ++index_;
            advance();
            return *this;
        }

        bool operator==(const iterator& o) const { return index_ == o.index_; }
        bool operator!=(const iterator& o) const { return index_ != o.index_; }
    };

    class const_iterator {
        friend class FlatHashMap;
        const Slot* slots_;
        size_t index_;
        size_t capacity_;

        void advance() {
            while (index_ < capacity_ && slots_[index_].state != OCCUPIED) {
                ++index_;
            }
        }
    public:
        const_iterator(const Slot* s, size_t i, size_t c) : slots_(s), index_(i), capacity_(c) { advance(); }

        const value_type& operator*() const { return slots_[index_].kv(); }
        const value_type* operator->() const { return &slots_[index_].kv(); }

        const_iterator& operator++() {
            ++index_;
            advance();
            return *this;
        }

        bool operator==(const const_iterator& o) const { return index_ == o.index_; }
        bool operator!=(const const_iterator& o) const { return index_ != o.index_; }
    };

    iterator begin() { return iterator(slots_, 0, capacity_); }
    iterator end() { return iterator(slots_, capacity_, capacity_); }
    const_iterator begin() const { return const_iterator(slots_, 0, capacity_); }
    const_iterator end() const { return const_iterator(slots_, capacity_, capacity_); }

    iterator find(const K& key) {
        size_t idx = slot_index(static_cast<uint64_t>(key));
        while (true) {
            if (slots_[idx].state == EMPTY) {
                return end();
            }
            if (slots_[idx].state == OCCUPIED && slots_[idx].kv().first == key) {
                // Return iterator that points directly at this slot (skip advance)
                iterator it(slots_, capacity_, capacity_); // end
                it.index_ = idx;
                return it;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    const_iterator find(const K& key) const {
        size_t idx = slot_index(static_cast<uint64_t>(key));
        while (true) {
            if (slots_[idx].state == EMPTY) {
                return end();
            }
            if (slots_[idx].state == OCCUPIED && slots_[idx].kv().first == key) {
                const_iterator it(slots_, capacity_, capacity_);
                it.index_ = idx;
                return it;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    V& operator[](const K& key) {
        // Check load factor first
        if (LOB_UNLIKELY(used_ + 1 > static_cast<size_t>(capacity_ * MAX_LOAD))) {
            grow_and_rehash();
        }

        size_t idx = slot_index(static_cast<uint64_t>(key));
        size_t first_tombstone = capacity_; // sentinel

        while (true) {
            if (slots_[idx].state == EMPTY) {
                // Insert at tombstone if we found one, else here
                size_t insert_idx = (first_tombstone < capacity_) ? first_tombstone : idx;
                new (&slots_[insert_idx].kv()) value_type(key, V{});
                if (slots_[insert_idx].state != TOMBSTONE) ++used_;
                slots_[insert_idx].state = OCCUPIED;
                ++size_;
                return slots_[insert_idx].kv().second;
            }
            if (slots_[idx].state == TOMBSTONE && first_tombstone == capacity_) {
                first_tombstone = idx;
            }
            if (slots_[idx].state == OCCUPIED && slots_[idx].kv().first == key) {
                return slots_[idx].kv().second;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    // Erase by key
    size_t erase(const K& key) {
        size_t idx = slot_index(static_cast<uint64_t>(key));
        while (true) {
            if (slots_[idx].state == EMPTY) return 0;
            if (slots_[idx].state == OCCUPIED && slots_[idx].kv().first == key) {
                slots_[idx].kv().~value_type();
                slots_[idx].state = TOMBSTONE;
                --size_;
                return 1;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    // Erase by iterator
    iterator erase(iterator it) {
        if (it == end()) return end();
        slots_[it.index_].kv().~value_type();
        slots_[it.index_].state = TOMBSTONE;
        --size_;
        ++it;
        return it;
    }
};

#endif // LOB_FLAT_HASH_MAP_H
