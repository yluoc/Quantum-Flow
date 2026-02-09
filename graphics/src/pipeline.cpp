#include "render/pipeline.h"
#include <algorithm>
#include <cmath>
#include <cassert>
#include <future>
#include <numeric>
#include <chrono>

namespace engine::render::fast {

// ═══════════════════════════════════════════════
// Singleton Instances
// ═══════════════════════════════════════════════
FastRenderPipeline& FastRenderPipeline::instance() {
    static FastRenderPipeline inst;
    return inst;
}

ParallelJobSystem& ParallelJobSystem::instance() {
    static ParallelJobSystem inst;
    return inst;
}

FastRenderPipeline::FastRenderPipeline() {
    // Initialize scene graph
    m_sceneGraph.clear();
    
    // Pre-allocate vectors
    m_drawCalls.reserve(4096);
    m_batches.reserve(256);
    m_instanceData.reserve(8192);
    m_visibleNodes.reserve(8192);
    
    // Initialize default transforms
    for (size_t i = 0; i < SceneGraphSoA::MAX_NODES; ++i) {
        m_sceneGraph.positions[i] = Vec3::zero();
        m_sceneGraph.rotations[i] = Vec3::zero();
        m_sceneGraph.scales[i] = Vec3::one();
        m_sceneGraph.localTransforms[i] = Mat4::identity();
        m_sceneGraph.worldTransforms[i] = Mat4::identity();
        m_sceneGraph.parents[i] = InvalidNode;
        m_sceneGraph.firstChild[i] = 0;
        m_sceneGraph.nextSibling[i] = 0;
        m_sceneGraph.childCount[i] = 0;
        m_sceneGraph.meshes[i] = InvalidMesh;
        m_sceneGraph.materials[i] = InvalidMaterial;
        m_sceneGraph.flags[i] = SceneGraphSoA::FLAG_VISIBLE;
    }
}

void FastRenderPipeline::setThreadCount(unsigned count) {
    m_threadCount = std::max(1u, count);
}

void ParallelJobSystem::setThreadCount(unsigned count) {
    m_threadCount = std::max(1u, count);
}


// ═══════════════════════════════════════════════
// Node Management
// ═══════════════════════════════════════════════
NodeHandle FastRenderPipeline::createNode() {
    uint32_t idx;

    // Reuse a destroyed index if available
    if (!m_freeIndices.empty()) {
        idx = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        idx = m_sceneGraph.nodeCount.fetch_add(1, std::memory_order_acq_rel);
        if (idx >= SceneGraphSoA::MAX_NODES) {
            m_sceneGraph.nodeCount.fetch_sub(1, std::memory_order_relaxed);
            return InvalidNode;
        }
    }

    // Initialize node
    m_sceneGraph.positions[idx] = Vec3::zero();
    m_sceneGraph.rotations[idx] = Vec3::zero();
    m_sceneGraph.scales[idx] = Vec3::one();
    m_sceneGraph.localTransforms[idx] = Mat4::identity();
    m_sceneGraph.worldTransforms[idx] = Mat4::identity();
    m_sceneGraph.parents[idx] = InvalidNode;
    m_sceneGraph.meshes[idx] = InvalidMesh;
    m_sceneGraph.materials[idx] = InvalidMaterial;
    m_sceneGraph.flags[idx] = SceneGraphSoA::FLAG_DIRTY | SceneGraphSoA::FLAG_VISIBLE;

    return idx + 1;  // Handle is 1-indexed (0 = invalid)
}

void FastRenderPipeline::destroyNode(NodeHandle handle) {
    if (handle == InvalidNode) return;
    uint32_t idx = handle - 1;

    // Mark as invisible and clear mesh
    m_sceneGraph.flags[idx] = 0;
    m_sceneGraph.meshes[idx] = InvalidMesh;

    // Reclaim index for reuse
    m_freeIndices.push_back(idx);
}

void FastRenderPipeline::setPosition(NodeHandle handle, const Vec3& pos) {
    if (handle == InvalidNode) return;
    uint32_t idx = handle - 1;
    m_sceneGraph.positions[idx] = pos;
    m_sceneGraph.setDirty(idx, true);
}

void FastRenderPipeline::setRotation(NodeHandle handle, const Vec3& rot) {
    if (handle == InvalidNode) return;
    uint32_t idx = handle - 1;
    m_sceneGraph.rotations[idx] = rot;
    m_sceneGraph.setDirty(idx, true);
}

void FastRenderPipeline::setScale(NodeHandle handle, const Vec3& scale) {
    if (handle == InvalidNode) return;
    uint32_t idx = handle - 1;
    m_sceneGraph.scales[idx] = scale;
    m_sceneGraph.setDirty(idx, true);
}

void FastRenderPipeline::setMesh(NodeHandle handle, MeshHandle mesh) {
    if (handle == InvalidNode) return;
    m_sceneGraph.meshes[handle - 1] = mesh;
}

void FastRenderPipeline::setMaterial(NodeHandle handle, MaterialHandle material) {
    if (handle == InvalidNode) return;
    m_sceneGraph.materials[handle - 1] = material;
}

void FastRenderPipeline::setParent(NodeHandle child, NodeHandle parent) {
    if (child == InvalidNode) return;
    uint32_t childIdx = child - 1;
    m_sceneGraph.parents[childIdx] = parent;
    m_sceneGraph.setDirty(childIdx, true);
}

void FastRenderPipeline::setVisible(NodeHandle handle, bool visible) {
    if (handle == InvalidNode) return;
    m_sceneGraph.setVisible(handle - 1, visible);
}


// ═══════════════════════════════════════════════
// Camera
// ═══════════════════════════════════════════════
void FastRenderPipeline::setCamera(const Vec3& eye, const Vec3& target, const Vec3& up,
                                   float fovDeg, float aspect, float nearP, float farP) {
    m_cameraPos = eye;
    m_viewMatrix = Mat4::lookAt(eye, target, up);
    m_projMatrix = Mat4::perspective(radians(fovDeg), aspect, nearP, farP);
    m_viewProjMatrix = m_projMatrix * m_viewMatrix;
    
    extractFrustumPlanes();
}


// ═══════════════════════════════════════════════
// Frustum Plane Extraction (Gribb-Hartmann method)
// ═══════════════════════════════════════════════
void FastRenderPipeline::extractFrustumPlanes() {
    const Mat4& vp = m_viewProjMatrix;
    
    // Extract rows from column-major matrix
    auto getRow = [&](int r) -> Vec4 {
        return Vec4(
            vp.col[0].data[r],
            vp.col[1].data[r],
            vp.col[2].data[r],
            vp.col[3].data[r]
        );
    };
    
    Vec4 r0 = getRow(0), r1 = getRow(1), r2 = getRow(2), r3 = getRow(3);
    
    // Plane extraction
    auto makePlane = [](float x, float y, float z, float w) -> Vec4 {
        float len = std::sqrt(x*x + y*y + z*z);
        float inv = (len > 1e-10f) ? 1.0f / len : 0.0f;
        return Vec4(x * inv, y * inv, z * inv, w * inv);
    };
    
    m_frustum.planes[0] = makePlane(r3.x+r0.x, r3.y+r0.y, r3.z+r0.z, r3.w+r0.w); // Left
    m_frustum.planes[1] = makePlane(r3.x-r0.x, r3.y-r0.y, r3.z-r0.z, r3.w-r0.w); // Right
    m_frustum.planes[2] = makePlane(r3.x+r1.x, r3.y+r1.y, r3.z+r1.z, r3.w+r1.w); // Bottom
    m_frustum.planes[3] = makePlane(r3.x-r1.x, r3.y-r1.y, r3.z-r1.z, r3.w-r1.w); // Top
    m_frustum.planes[4] = makePlane(r3.x+r2.x, r3.y+r2.y, r3.z+r2.z, r3.w+r2.w); // Near
    m_frustum.planes[5] = makePlane(r3.x-r2.x, r3.y-r2.y, r3.z-r2.z, r3.w-r2.w); // Far
}


// ═══════════════════════════════════════════════
// Transform Update - SIMD Batch Processing
// ═══════════════════════════════════════════════
void FastRenderPipeline::updateTransforms() {
    MicrosecondTimer timer;
    timer.start();
    
    uint32_t count = m_sceneGraph.nodeCount.load(std::memory_order_acquire);
    m_stats.totalNodes.store(count, std::memory_order_relaxed);
    
    if (count == 0) return;
    
    // Process in parallel batches
    constexpr size_t BATCH_SIZE = 256;
    
    if (count > BATCH_SIZE && m_threadCount > 1) {
        std::vector<std::future<void>> futures;
        futures.reserve(m_threadCount);
        
        size_t perThread = (count + m_threadCount - 1) / m_threadCount;
        
        for (unsigned t = 0; t < m_threadCount; ++t) {
            size_t start = t * perThread;
            size_t end = std::min(start + perThread, static_cast<size_t>(count));
            
            if (start < end) {
                futures.push_back(std::async(std::launch::async, [this, start, end]() {
                    updateTransformsSIMD(static_cast<uint32_t>(start), static_cast<uint32_t>(end));
                }));
            }
        }
        
        for (auto& f : futures) f.get();
    } else {
        updateTransformsSIMD(0, count);
    }
    
    m_stats.transformUpdateUs.store(timer.elapsedUs(), std::memory_order_relaxed);
}

void FastRenderPipeline::updateTransformsSIMD(uint32_t start, uint32_t end) {
    // Process transforms with SIMD optimization
    for (uint32_t i = start; i < end; ++i) {
        if (!m_sceneGraph.isDirty(i)) continue;
        
        const Vec3& pos = m_sceneGraph.positions[i];
        const Vec3& rot = m_sceneGraph.rotations[i];
        const Vec3& scale = m_sceneGraph.scales[i];
        
        // Build local transform: T * Ry * Rx * Rz * S
        Mat4 local = Mat4::translate(pos)
                   * Mat4::rotateY(rot.y)
                   * Mat4::rotateX(rot.x)
                   * Mat4::rotateZ(rot.z)
                   * Mat4::scale(scale);
        
        m_sceneGraph.localTransforms[i] = local;
        
        // Compute world transform
        NodeHandle parentHandle = m_sceneGraph.parents[i];
        if (parentHandle != InvalidNode) {
            uint32_t parentIdx = parentHandle - 1;
            m_sceneGraph.worldTransforms[i] = m_sceneGraph.worldTransforms[parentIdx] * local;
        } else {
            m_sceneGraph.worldTransforms[i] = local;
        }
        
        m_sceneGraph.setDirty(i, false);
    }
}


// ═══════════════════════════════════════════════
// Frustum Culling - SIMD Batch Test
// ═══════════════════════════════════════════════
void FastRenderPipeline::frustumCull() {
    MicrosecondTimer timer;
    timer.start();
    
    m_visibility.clear();
    m_visibleNodes.clear();
    
    uint32_t count = m_sceneGraph.nodeCount.load(std::memory_order_acquire);
    uint32_t visible = 0;
    
    // Simple AABB test against frustum planes
    for (uint32_t i = 0; i < count; ++i) {
        if (!m_sceneGraph.isVisible(i)) continue;
        if (m_sceneGraph.meshes[i] == InvalidMesh) continue;
        
        // Get world-space position as AABB center approximation
        const Vec3& pos = m_sceneGraph.worldTransforms[i].col[3].xyz();
        
        // Simple sphere-frustum test (fast approximation)
        bool inside = true;
        float radius = 2.0f;  // Approximate bounding radius
        
        for (int p = 0; p < 6 && inside; ++p) {
            const Vec4& plane = m_frustum.planes[p];
            float dist = plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w;
            if (dist < -radius) {
                inside = false;
            }
        }
        
        if (inside) {
            m_visibility.setVisible(i, true);
            m_visibleNodes.push_back(i + 1);  // Convert to handle
            ++visible;
        }
    }
    
    m_stats.visibleNodes.store(visible, std::memory_order_relaxed);
    m_stats.culledNodes.store(count - visible, std::memory_order_relaxed);
    m_stats.frustumCullUs.store(timer.elapsedUs(), std::memory_order_relaxed);
}


// ═══════════════════════════════════════════════
// Draw Call Building - Sort-Free Batching
// ═══════════════════════════════════════════════
void FastRenderPipeline::buildDrawCalls() {
    MicrosecondTimer timer;
    timer.start();
    
    m_drawCalls.clear();
    m_batches.clear();
    m_instanceData.clear();
    
    // Group by material (use counting sort for O(n) complexity)
    struct MaterialGroup {
        MaterialHandle material;
        std::vector<uint32_t> nodeIndices;
    };
    
    std::unordered_map<MaterialHandle, MaterialGroup> groups;
    
    for (NodeHandle handle : m_visibleNodes) {
        uint32_t idx = handle - 1;
        MaterialHandle mat = m_sceneGraph.materials[idx];
        if (mat == InvalidMaterial) mat = 1;  // Default material
        
        auto& group = groups[mat];
        group.material = mat;
        group.nodeIndices.push_back(idx);
    }
    
    // Build batches
    uint32_t instanceOffset = 0;
    
    for (auto& [matHandle, group] : groups) {
        // Sub-group by mesh for instancing
        std::unordered_map<MeshHandle, std::vector<uint32_t>> meshGroups;
        
        for (uint32_t idx : group.nodeIndices) {
            MeshHandle mesh = m_sceneGraph.meshes[idx];
            meshGroups[mesh].push_back(idx);
        }
        
        DrawBatch batch;
        batch.material = matHandle;
        batch.firstDrawCall = static_cast<uint32_t>(m_drawCalls.size());
        batch.drawCallCount = 0;
        
        for (auto& [meshHandle, indices] : meshGroups) {
            DrawCall dc;
            dc.mesh = meshHandle;
            dc.material = matHandle;
            dc.instanceCount = static_cast<uint32_t>(indices.size());
            dc.firstInstance = instanceOffset;
            
            // Store instance data
            for (uint32_t idx : indices) {
                InstanceData inst;
                inst.worldTransform = m_sceneGraph.worldTransforms[idx];
                inst.normalMatrix = inst.worldTransform.inverse3x3().transpose();
                m_instanceData.push_back(inst);
            }
            
            instanceOffset += dc.instanceCount;
            m_drawCalls.push_back(dc);
            ++batch.drawCallCount;
        }
        
        m_batches.push_back(batch);
    }
    
    m_stats.drawCalls.store(static_cast<uint32_t>(m_drawCalls.size()), std::memory_order_relaxed);
    m_stats.instancesDrawn.store(instanceOffset, std::memory_order_relaxed);
    m_stats.drawBuildUs.store(timer.elapsedUs(), std::memory_order_relaxed);
}


// ═══════════════════════════════════════════════
// Submit Draw Calls
// ═══════════════════════════════════════════════
void FastRenderPipeline::submit() {
    // In a real implementation, this would:
    // 1. Bind instance buffer
    // 2. For each batch: bind material, issue draw calls
    
    // Simulated GPU submission (instant in CPU-only mode)
    for (const auto& batch : m_batches) {
        for (uint32_t i = 0; i < batch.drawCallCount; ++i) {
            const DrawCall& dc = m_drawCalls[batch.firstDrawCall + i];
            // glDrawElementsInstanced(..., dc.instanceCount, ...)
            (void)dc;  // Suppress unused warning
        }
    }
}


// ═══════════════════════════════════════════════
// Full Frame Render
// ═══════════════════════════════════════════════
void FastRenderPipeline::renderFrame() {
    m_stats.reset();
    m_frameArena.reset();
    
    updateTransforms();
    frustumCull();
    buildDrawCalls();
    submit();
}


// ═══════════════════════════════════════════════
// Parallel Job System
// ═══════════════════════════════════════════════
void ParallelJobSystem::parallelFor(size_t count, size_t batchSize,
                                    const std::function<void(size_t, size_t)>& func) {
    if (count == 0) return;
    
    size_t numBatches = (count + batchSize - 1) / batchSize;
    
    if (numBatches <= 1 || m_threadCount <= 1) {
        func(0, count);
        return;
    }
    
    std::vector<std::future<void>> futures;
    futures.reserve(numBatches);
    
    for (size_t i = 0; i < numBatches; ++i) {
        size_t start = i * batchSize;
        size_t end = std::min(start + batchSize, count);
        
        futures.push_back(std::async(std::launch::async, [&func, start, end]() {
            func(start, end);
        }));
    }
    
    for (auto& f : futures) f.get();
}

} // namespace engine::render::fast