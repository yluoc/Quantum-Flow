#ifndef LOB_SLAB_POOL_H
#define LOB_SLAB_POOL_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cassert>
#include <new>
#include "Macros.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

/**
 * SlabPool: O(1) free-list based object pool allocator for hot-path allocations.
 *
 * Template parameter SLAB_SZ controls objects per slab.
 * On Linux, slab storage is allocated via mmap + madvise(MADV_HUGEPAGE)
 * for TLB-friendly large allocations. Falls back to aligned new otherwise.
 */
template<typename T, size_t SLAB_SZ = 1024>
class SlabPool {
private:
    static constexpr size_t ALIGNMENT = alignof(T) > alignof(std::max_align_t)
                                        ? alignof(T)
                                        : alignof(std::max_align_t);
    static constexpr size_t OBJECT_SIZE = sizeof(T);
    static constexpr size_t SLAB_BYTES = SLAB_SZ * OBJECT_SIZE;

    union FreeNode {
        char storage[OBJECT_SIZE];
        FreeNode* next;
        FreeNode() : next(nullptr) {}
    };

    struct Slab {
        char* storage;   // pointer to allocated storage
        bool use_mmap;   // track allocation method for cleanup

        Slab() : storage(nullptr), use_mmap(false) {
            allocate_storage();
        }

        ~Slab() {
            free_storage();
        }

        void allocate_storage() {
#ifdef __linux__
            void* ptr = ::mmap(nullptr, SLAB_BYTES,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS,
                               -1, 0);
            if (ptr != MAP_FAILED) {
                ::madvise(ptr, SLAB_BYTES, MADV_HUGEPAGE);
                storage = static_cast<char*>(ptr);
                use_mmap = true;
                return;
            }
#endif
            // Fallback: aligned allocation
            storage = static_cast<char*>(::operator new(SLAB_BYTES, std::align_val_t{ALIGNMENT}));
            use_mmap = false;
        }

        void free_storage() {
            if (!storage) return;
#ifdef __linux__
            if (use_mmap) {
                ::munmap(storage, SLAB_BYTES);
                storage = nullptr;
                return;
            }
#endif
            ::operator delete(storage, std::align_val_t{ALIGNMENT});
            storage = nullptr;
        }

        Slab(const Slab&) = delete;
        Slab& operator=(const Slab&) = delete;
    };

    std::vector<Slab*> slabs_;
    FreeNode* free_list_;
    size_t total_capacity_;
    size_t allocated_count_;

    static FreeNode* to_free_node(T* obj) {
        return reinterpret_cast<FreeNode*>(obj);
    }

    static T* to_object(FreeNode* node) {
        return reinterpret_cast<T*>(node);
    }

#ifndef NDEBUG
    Slab* find_slab(T* obj) {
        char* obj_ptr = reinterpret_cast<char*>(obj);
        for (auto* slab : slabs_) {
            char* slab_start = slab->storage;
            char* slab_end = slab->storage + SLAB_BYTES;
            if (obj_ptr >= slab_start && obj_ptr < slab_end) {
                return slab;
            }
        }
        return nullptr;
    }
#endif

public:
    SlabPool(size_t initial_capacity = SLAB_SZ)
        : free_list_(nullptr),
          total_capacity_(0),
          allocated_count_(0) {
        size_t initial_slabs = (initial_capacity + SLAB_SZ - 1) / SLAB_SZ;
        for (size_t i = 0; i < initial_slabs; ++i) {
            add_slab();
        }
    }

    ~SlabPool() {
        for (auto* slab : slabs_) {
            for (size_t i = 0; i < SLAB_SZ; ++i) {
                T* obj = reinterpret_cast<T*>(slab->storage + i * OBJECT_SIZE);
                bool in_free_list = false;
                for (FreeNode* node = free_list_; node; node = node->next) {
                    if (to_free_node(obj) == node) {
                        in_free_list = true;
                        break;
                    }
                }
                if (!in_free_list) {
                    obj->~T();
                }
            }
            delete slab;
        }
    }

    SlabPool(const SlabPool&) = delete;
    SlabPool& operator=(const SlabPool&) = delete;

    void add_slab() {
        Slab* slab = new Slab();
        slabs_.push_back(slab);
        total_capacity_ += SLAB_SZ;

        for (size_t i = 0; i < SLAB_SZ; ++i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(slab->storage + i * OBJECT_SIZE);
            node->next = free_list_;
            free_list_ = node;
        }
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        if (LOB_UNLIKELY(!free_list_)) {
            add_slab();
        }

        FreeNode* node = free_list_;
        free_list_ = node->next;

        T* obj = to_object(node);
        new (obj) T(std::forward<Args>(args)...);
        ++allocated_count_;

        return obj;
    }

    void deallocate(T* obj) {
        if (LOB_UNLIKELY(!obj)) return;

#ifndef NDEBUG
        assert(find_slab(obj) != nullptr && "Object not allocated from this pool");
#endif

        obj->~T();

        FreeNode* node = to_free_node(obj);
        node->next = free_list_;
        free_list_ = node;
        --allocated_count_;
    }

    size_t capacity() const { return total_capacity_; }
    size_t size() const { return allocated_count_; }
};

#endif // LOB_SLAB_POOL_H
