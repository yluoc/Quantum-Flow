#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <array>
#include <utility>
#include <type_traits>
#include <stdexcept>
#include <new>

namespace engine {
namespace memory {
namespace fast {

// ═══════════════════════════════════════════════
// Platform-specific aligned allocation
// ═══════════════════════════════════════════════
inline void* alignedAlloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) ptr = nullptr;
#endif
    return ptr;
}

inline void alignedFree(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}


// ═══════════════════════════════════════════════
// Lock-Free Free List (for ultra-fast allocation)
// Uses compare-and-swap for thread-safe O(1) alloc/free
// ═══════════════════════════════════════════════
template<typename T>
class LockFreePool {
    static_assert(sizeof(T) >= sizeof(void*), "T must be at least pointer size");
    
    struct Node {
        std::atomic<Node*> next;
    };

    std::atomic<Node*> m_freeHead{nullptr};
    std::atomic<size_t> m_allocated{0};
    std::atomic<size_t> m_capacity{0};
    
    void* m_buffer{nullptr};
    size_t m_blockSize;
    size_t m_maxBlocks;

public:
    explicit LockFreePool(size_t maxBlocks = 4096) 
        : m_blockSize(std::max(sizeof(T), sizeof(Node)))
        , m_maxBlocks(maxBlocks)
    {
        // Allocate cache-line aligned buffer
        size_t bufferSize = m_maxBlocks * m_blockSize;
        m_buffer = alignedAlloc(bufferSize, 64);
        if (!m_buffer) throw std::bad_alloc();
        
        m_capacity.store(m_maxBlocks, std::memory_order_release);
        
        // Pre-link all blocks into free list
        for (size_t i = 0; i < m_maxBlocks; ++i) {
            Node* node = reinterpret_cast<Node*>(
                static_cast<char*>(m_buffer) + i * m_blockSize
            );
            Node* nextNode = (i + 1 < m_maxBlocks) 
                ? reinterpret_cast<Node*>(static_cast<char*>(m_buffer) + (i + 1) * m_blockSize)
                : nullptr;
            node->next.store(nextNode, std::memory_order_relaxed);
        }
        m_freeHead.store(reinterpret_cast<Node*>(m_buffer), std::memory_order_release);
    }

    ~LockFreePool() {
        alignedFree(m_buffer);
    }

    // Non-copyable, non-movable
    LockFreePool(const LockFreePool&) = delete;
    LockFreePool& operator=(const LockFreePool&) = delete;

    // Lock-free allocation - O(1) amortized
    T* allocate() {
        Node* oldHead = m_freeHead.load(std::memory_order_acquire);
        
        while (oldHead) {
            Node* nextNode = oldHead->next.load(std::memory_order_relaxed);
            
            if (m_freeHead.compare_exchange_weak(oldHead, nextNode,
                    std::memory_order_release, std::memory_order_acquire)) {
                m_allocated.fetch_add(1, std::memory_order_relaxed);
                return reinterpret_cast<T*>(oldHead);
            }
            // CAS failed, oldHead updated by compare_exchange_weak, retry
        }
        
        // Pool exhausted
        return nullptr;
    }

    // Lock-free deallocation - O(1)
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        Node* node = reinterpret_cast<Node*>(ptr);
        Node* oldHead = m_freeHead.load(std::memory_order_acquire);
        
        do {
            node->next.store(oldHead, std::memory_order_relaxed);
        } while (!m_freeHead.compare_exchange_weak(oldHead, node,
                    std::memory_order_release, std::memory_order_acquire));
        
        m_allocated.fetch_sub(1, std::memory_order_relaxed);
    }

    template<typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        if (ptr) {
            new (ptr) T(std::forward<Args>(args)...);
        }
        return ptr;
    }

    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            deallocate(ptr);
        }
    }

    size_t size() const { return m_allocated.load(std::memory_order_relaxed); }
    size_t capacity() const { return m_capacity.load(std::memory_order_relaxed); }
};


// ═══════════════════════════════════════════════
// Thread-Local Arena Allocator
// Zero-contention bump allocator for per-frame temporaries
// ═══════════════════════════════════════════════
class alignas(64) ThreadLocalArena {
    char* m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_offset{0};

public:
    explicit ThreadLocalArena(size_t capacity = 4 * 1024 * 1024)  // 4MB default
        : m_capacity(capacity)
    {
        m_buffer = static_cast<char*>(alignedAlloc(capacity, 64));
        if (!m_buffer) throw std::bad_alloc();
    }

    ~ThreadLocalArena() {
        alignedFree(m_buffer);
    }

    ThreadLocalArena(const ThreadLocalArena&) = delete;
    ThreadLocalArena& operator=(const ThreadLocalArena&) = delete;

    // Ultra-fast allocation (single atomic add)
    void* allocate(size_t size, size_t alignment = 8) {
        size_t currentOffset = m_offset.load(std::memory_order_relaxed);
        size_t aligned;
        
        do {
            aligned = (currentOffset + alignment - 1) & ~(alignment - 1);
            if (aligned + size > m_capacity) {
                return nullptr;  // Arena exhausted
            }
        } while (!m_offset.compare_exchange_weak(currentOffset, aligned + size,
                    std::memory_order_release, std::memory_order_relaxed));
        
        return m_buffer + aligned;
    }

    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        T* ptr = static_cast<T*>(allocate(sizeof(T), alignof(T)));
        if (ptr) {
            new (ptr) T(std::forward<Args>(args)...);
        }
        return ptr;
    }

    template<typename T>
    T* allocateArray(size_t count) {
        return static_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    // O(1) reset - instant free of all allocations
    void reset() {
        m_offset.store(0, std::memory_order_release);
    }

    size_t used() const { return m_offset.load(std::memory_order_relaxed); }
    size_t capacity() const { return m_capacity; }
    float usageRatio() const { return static_cast<float>(used()) / m_capacity; }
};


// ═══════════════════════════════════════════════
// SLAB Allocator - Fixed-size blocks with size classes
// Microsecond allocation for common object sizes
// ═══════════════════════════════════════════════
class SlabAllocator {
    // Size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes
    static constexpr size_t NUM_SIZE_CLASSES = 8;
    static constexpr size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
        16, 32, 64, 128, 256, 512, 1024, 2048
    };
    
    static constexpr size_t BLOCKS_PER_SLAB = 256;

    struct Slab {
        std::atomic<uint64_t> bitmap[4];  // 256 bits for 256 blocks
        char* data;
        size_t blockSize;
        
        Slab(size_t blkSize) : blockSize(blkSize) {
            for (auto& b : bitmap) b.store(0, std::memory_order_relaxed);
            data = static_cast<char*>(alignedAlloc(BLOCKS_PER_SLAB * blkSize, 64));
            if (!data) throw std::bad_alloc();
        }
        
        ~Slab() { alignedFree(data); }
        
        void* tryAllocate() {
            for (int word = 0; word < 4; ++word) {
                uint64_t bits = bitmap[word].load(std::memory_order_acquire);
                while (bits != ~0ULL) {
                    // Find first zero bit
                    int bit = __builtin_ctzll(~bits);
                    uint64_t mask = 1ULL << bit;
                    
                    if (bitmap[word].compare_exchange_weak(bits, bits | mask,
                            std::memory_order_release, std::memory_order_acquire)) {
                        size_t index = word * 64 + bit;
                        return data + index * blockSize;
                    }
                    // CAS failed, bits updated, retry
                }
            }
            return nullptr;  // Slab full
        }
        
        bool tryDeallocate(void* ptr) {
            ptrdiff_t offset = static_cast<char*>(ptr) - data;
            if (offset < 0 || offset >= static_cast<ptrdiff_t>(BLOCKS_PER_SLAB * blockSize)) {
                return false;
            }
            
            size_t index = offset / blockSize;
            int word = index / 64;
            int bit = index % 64;
            uint64_t mask = 1ULL << bit;
            
            bitmap[word].fetch_and(~mask, std::memory_order_release);
            return true;
        }
    };

    std::array<Slab*, NUM_SIZE_CLASSES> m_slabs;

    static size_t getSizeClass(size_t size) {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            if (size <= SIZE_CLASSES[i]) return i;
        }
        return NUM_SIZE_CLASSES;  // Too large
    }

public:
    SlabAllocator() {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            m_slabs[i] = new Slab(SIZE_CLASSES[i]);
        }
    }

    ~SlabAllocator() {
        for (auto* slab : m_slabs) {
            delete slab;
        }
    }

    void* allocate(size_t size) {
        size_t sizeClass = getSizeClass(size);
        if (sizeClass >= NUM_SIZE_CLASSES) {
            // Fall back to regular allocation for large objects
            return alignedAlloc(size, 16);
        }
        
        void* ptr = m_slabs[sizeClass]->tryAllocate();
        if (!ptr) {
            // Slab exhausted - could grow here
            return alignedAlloc(size, 16);
        }
        return ptr;
    }

    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        
        size_t sizeClass = getSizeClass(size);
        if (sizeClass >= NUM_SIZE_CLASSES) {
            alignedFree(ptr);
            return;
        }
        
        if (!m_slabs[sizeClass]->tryDeallocate(ptr)) {
            // Pointer wasn't from this slab
            alignedFree(ptr);
        }
    }

    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        void* ptr = allocate(sizeof(T));
        if (ptr) {
            new (ptr) T(std::forward<Args>(args)...);
        }
        return static_cast<T*>(ptr);
    }

    template<typename T>
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            deallocate(ptr, sizeof(T));
        }
    }
};


// ═══════════════════════════════════════════════
// Cache-Line Padded Atomic Counter
// Prevents false sharing in multi-threaded scenarios
// ═══════════════════════════════════════════════
template<typename T>
struct alignas(64) CacheLineAtomic {
    std::atomic<T> value{0};
    char padding[64 - sizeof(std::atomic<T>)];
    
    T load(std::memory_order order = std::memory_order_seq_cst) const {
        return value.load(order);
    }
    
    void store(T v, std::memory_order order = std::memory_order_seq_cst) {
        value.store(v, order);
    }
    
    T fetch_add(T v, std::memory_order order = std::memory_order_seq_cst) {
        return value.fetch_add(v, order);
    }
};


// ═══════════════════════════════════════════════
// Ring Buffer for Lock-Free Producer/Consumer
// ═══════════════════════════════════════════════
template<typename T, size_t Capacity>
class alignas(64) LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
    alignas(64) std::array<T, Capacity> m_buffer;

public:
    bool tryPush(const T& item) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        size_t nextTail = (tail + 1) & (Capacity - 1);
        
        if (nextTail == m_head.load(std::memory_order_acquire)) {
            return false;  // Full
        }
        
        m_buffer[tail] = item;
        m_tail.store(nextTail, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        
        if (head == m_tail.load(std::memory_order_acquire)) {
            return false;  // Empty
        }
        
        item = m_buffer[head];
        m_head.store((head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return m_head.load(std::memory_order_relaxed) == 
               m_tail.load(std::memory_order_relaxed);
    }

    size_t size() const {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t tail = m_tail.load(std::memory_order_relaxed);
        return (tail - head) & (Capacity - 1);
    }
};

} // namespace fast
} // namespace memory
} // namespace engine