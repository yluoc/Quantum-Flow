#pragma once

#include "math/simd_math.h"
#include "memory/allocator.h"
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <array>
#include <unordered_map>
#include <functional>

namespace engine {
namespace render {
namespace fast {

using namespace engine::math::fast;

// ═══════════════════════════════════════════════
// Handles (type-safe indices)
// ═══════════════════════════════════════════════
using NodeHandle = uint32_t;
using MeshHandle = uint32_t;
using MaterialHandle = uint32_t;
using ShaderHandle = uint32_t;

constexpr NodeHandle InvalidNode = 0;
constexpr MeshHandle InvalidMesh = 0;
constexpr MaterialHandle InvalidMaterial = 0;

// ═══════════════════════════════════════════════
// Data-Oriented Scene Graph (Structure of Arrays)
// Cache-friendly layout for batch processing
// ═══════════════════════════════════════════════
struct alignas(64) SceneGraphSoA {
    static constexpr size_t MAX_NODES = 65536;
    
    // Transform components (hot data - accessed every frame)
    alignas(64) std::array<Vec3, MAX_NODES> positions;
    alignas(64) std::array<Vec3, MAX_NODES> rotations;   // Euler angles
    alignas(64) std::array<Vec3, MAX_NODES> scales;
    
    // Computed transforms (updated during traversal)
    alignas(64) std::array<Mat4, MAX_NODES> localTransforms;
    alignas(64) std::array<Mat4, MAX_NODES> worldTransforms;
    
    // Hierarchy (cold data - rarely changes)
    alignas(64) std::array<NodeHandle, MAX_NODES> parents;
    alignas(64) std::array<uint16_t, MAX_NODES> firstChild;
    alignas(64) std::array<uint16_t, MAX_NODES> nextSibling;
    alignas(64) std::array<uint8_t, MAX_NODES> childCount;
    
    // Render data
    alignas(64) std::array<MeshHandle, MAX_NODES> meshes;
    alignas(64) std::array<MaterialHandle, MAX_NODES> materials;
    
    // Flags (packed for cache efficiency)
    alignas(64) std::array<uint8_t, MAX_NODES> flags;  // bit 0: dirty, bit 1: visible
    
    std::atomic<uint32_t> nodeCount{0};
    
    static constexpr uint8_t FLAG_DIRTY = 0x01;
    static constexpr uint8_t FLAG_VISIBLE = 0x02;
    
    void clear() {
        nodeCount.store(0, std::memory_order_release);
    }
    
    bool isDirty(uint32_t idx) const { return flags[idx] & FLAG_DIRTY; }
    bool isVisible(uint32_t idx) const { return flags[idx] & FLAG_VISIBLE; }
    void setDirty(uint32_t idx, bool v) { 
        if (v) flags[idx] |= FLAG_DIRTY; 
        else flags[idx] &= ~FLAG_DIRTY; 
    }
    void setVisible(uint32_t idx, bool v) { 
        if (v) flags[idx] |= FLAG_VISIBLE; 
        else flags[idx] &= ~FLAG_VISIBLE; 
    }
};


// ═══════════════════════════════════════════════
// Frustum Planes (SIMD-friendly layout)
// ═══════════════════════════════════════════════
struct alignas(64) FrustumPlanes {
    // Each plane: xyz = normal, w = distance
    Vec4 planes[6];  // Left, Right, Bottom, Top, Near, Far
};


// ═══════════════════════════════════════════════
// AABB Storage (SoA for batch culling)
// ═══════════════════════════════════════════════
struct alignas(64) AABBStorage {
    static constexpr size_t MAX_AABBS = 65536;
    
    alignas(64) std::array<Vec3, MAX_AABBS> mins;
    alignas(64) std::array<Vec3, MAX_AABBS> maxs;
    
    std::atomic<uint32_t> count{0};
};


// ═══════════════════════════════════════════════
// Draw Call (cache-line optimized)
// ═══════════════════════════════════════════════
struct alignas(64) DrawCall {
    MeshHandle mesh;
    MaterialHandle material;
    uint32_t instanceCount;
    uint32_t firstInstance;
    // World transform stored in instance buffer
};

struct DrawBatch {
    MaterialHandle material;
    uint32_t firstDrawCall;
    uint32_t drawCallCount;
};


// ═══════════════════════════════════════════════
// Instance Data (for GPU instancing)
// ═══════════════════════════════════════════════
struct alignas(64) InstanceData {
    Mat4 worldTransform;
    Mat4 normalMatrix;
};


// ═══════════════════════════════════════════════
// Visibility Results (bit-packed)
// ═══════════════════════════════════════════════
struct VisibilityBuffer {
    static constexpr size_t MAX_OBJECTS = 65536;
    alignas(64) std::array<uint64_t, MAX_OBJECTS / 64> bits;
    
    void clear() {
        std::memset(bits.data(), 0, bits.size() * sizeof(uint64_t));
    }
    
    bool isVisible(uint32_t idx) const {
        return (bits[idx / 64] >> (idx % 64)) & 1;
    }
    
    void setVisible(uint32_t idx, bool v) {
        if (v) bits[idx / 64] |= (1ULL << (idx % 64));
        else bits[idx / 64] &= ~(1ULL << (idx % 64));
    }
    
    // Count visible objects using popcount
    uint32_t countVisible() const {
        uint32_t count = 0;
        for (auto word : bits) {
            count += __builtin_popcountll(word);
        }
        return count;
    }
};


// ═══════════════════════════════════════════════
// Frame Statistics
// ═══════════════════════════════════════════════
struct alignas(64) FrameStats {
    std::atomic<uint32_t> totalNodes{0};
    std::atomic<uint32_t> culledNodes{0};
    std::atomic<uint32_t> visibleNodes{0};
    std::atomic<uint32_t> drawCalls{0};
    std::atomic<uint32_t> instancesDrawn{0};
    std::atomic<uint64_t> transformUpdateUs{0};  // microseconds
    std::atomic<uint64_t> frustumCullUs{0};
    std::atomic<uint64_t> drawBuildUs{0};
    
    void reset() {
        totalNodes.store(0, std::memory_order_relaxed);
        culledNodes.store(0, std::memory_order_relaxed);
        visibleNodes.store(0, std::memory_order_relaxed);
        drawCalls.store(0, std::memory_order_relaxed);
        instancesDrawn.store(0, std::memory_order_relaxed);
        transformUpdateUs.store(0, std::memory_order_relaxed);
        frustumCullUs.store(0, std::memory_order_relaxed);
        drawBuildUs.store(0, std::memory_order_relaxed);
    }
};


// ═══════════════════════════════════════════════
// High-Performance Render Pipeline
// ═══════════════════════════════════════════════
class FastRenderPipeline {
public:
    static FastRenderPipeline& instance();
    
    // ── Node Management ──
    NodeHandle createNode();
    void destroyNode(NodeHandle handle);
    
    void setPosition(NodeHandle handle, const Vec3& pos);
    void setRotation(NodeHandle handle, const Vec3& rot);
    void setScale(NodeHandle handle, const Vec3& scale);
    void setMesh(NodeHandle handle, MeshHandle mesh);
    void setMaterial(NodeHandle handle, MaterialHandle material);
    void setParent(NodeHandle child, NodeHandle parent);
    void setVisible(NodeHandle handle, bool visible);
    
    // ── Camera ──
    void setCamera(const Vec3& eye, const Vec3& target, const Vec3& up,
                   float fovDeg, float aspect, float nearP, float farP);
    
    // ── Per-Frame Pipeline (all microsecond-optimized) ──
    
    // 1. Update transforms - parallel, SIMD batched
    void updateTransforms();
    
    // 2. Frustum culling - SIMD batch test
    void frustumCull();
    
    // 3. Build draw calls - sort-free batching
    void buildDrawCalls();
    
    // 4. Submit - issue draw calls
    void submit();
    
    // Full frame (convenience)
    void renderFrame();
    
    // ── Stats ──
    const FrameStats& stats() const { return m_stats; }
    
    // ── Configuration ──
    void setThreadCount(unsigned count);
    
private:
    FastRenderPipeline();
    
    // ── Transform Update (parallel) ──
    void updateTransformsRange(uint32_t start, uint32_t end, const Mat4& parentWorld);
    void updateTransformsSIMD(uint32_t start, uint32_t end);
    
    // ── Frustum extraction ──
    void extractFrustumPlanes();
    
    // ── Scene Data ──
    SceneGraphSoA m_sceneGraph;
    AABBStorage m_aabbs;
    
    // ── Camera ──
    Mat4 m_viewMatrix;
    Mat4 m_projMatrix;
    Mat4 m_viewProjMatrix;
    Vec3 m_cameraPos;
    FrustumPlanes m_frustum;
    
    // ── Visibility ──
    VisibilityBuffer m_visibility;
    
    // ── Draw Calls ──
    std::vector<DrawCall> m_drawCalls;
    std::vector<DrawBatch> m_batches;
    std::vector<InstanceData> m_instanceData;
    std::vector<NodeHandle> m_visibleNodes;
    
    // ── Threading ──
    unsigned m_threadCount{4};
    
    // ── Stats ──
    FrameStats m_stats;
    
    // ── Free list for destroyed node indices ──
    std::vector<uint32_t> m_freeIndices;

    // ── Allocators ──
    memory::fast::ThreadLocalArena m_frameArena{8 * 1024 * 1024};  // 8MB per frame
};


// ═══════════════════════════════════════════════
// High-Resolution Timer for Microsecond Measurement
// ═══════════════════════════════════════════════
class MicrosecondTimer {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_start;
    
public:
    void start() { m_start = Clock::now(); }
    
    uint64_t elapsedUs() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - m_start
        ).count();
    }
    
    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(
            Clock::now() - m_start
        ).count();
    }
};


// ═══════════════════════════════════════════════
// Parallel Job System (for transform updates)
// ═══════════════════════════════════════════════
class ParallelJobSystem {
public:
    using Job = std::function<void()>;
    
    static ParallelJobSystem& instance();
    
    void setThreadCount(unsigned count);
    
    // Submit jobs and wait for completion
    void parallelFor(size_t count, size_t batchSize, 
                     const std::function<void(size_t start, size_t end)>& func);
    
private:
    unsigned m_threadCount{4};
    std::vector<std::thread> m_threads;
};

} // namespace fast
} // namespace render
} // namespace engine