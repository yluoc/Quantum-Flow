#include "gpu/vertex_manager.h"
#include <algorithm>
#include <cstring>
#include <cassert>
#include <numeric>

// ─────────────────────────────────────────────
// Static handle counters
// ─────────────────────────────────────────────
engine::gpu::BufferHandle engine::gpu::VertexBuffer::s_nextHandle = 1;
engine::gpu::BufferHandle engine::gpu::IndexBuffer::s_nextHandle  = 1;

// ═══════════════════════════════════════════════
// VertexBuffer
// ═══════════════════════════════════════════════
void engine::gpu::VertexBuffer::create(size_t stride, size_t initialCapacity) {
    assert(stride > 0);
    release();
    m_stride   = stride;
    m_count    = 0;
    m_capacity = 0;
    m_data     = nullptr;
    m_dirty    = false;
    m_handle   = s_nextHandle++;
    if (initialCapacity > 0) ensureCapacity(initialCapacity);
}

void engine::gpu::VertexBuffer::upload(const void* data, size_t count) {
    if (!data || count == 0) return;
    ensureCapacity(count);
    std::memcpy(static_cast<char*>(m_data), data, count * m_stride);
    m_count = count;
    m_dirty = true;
}

void engine::gpu::VertexBuffer::append(const void* data, size_t byteCount) {
    size_t newElements = byteCount / m_stride;
    ensureCapacity(m_count + newElements);
    std::memcpy(static_cast<char*>(m_data) + m_count * m_stride, data, byteCount);
    m_count += newElements;
    m_dirty  = true;
}

void* engine::gpu::VertexBuffer::mapRange(size_t startIndex, size_t count) {
    ensureCapacity(startIndex + count);
    if (startIndex + count > m_count) m_count = startIndex + count;
    m_dirty = true;
    return static_cast<char*>(m_data) + startIndex * m_stride;
}

void engine::gpu::VertexBuffer::finalize() {
    // In a real GPU driver this would issue a barrier / copy-to-GPU.
    // Here we just clear the dirty flag.
    m_dirty = false;
}

void engine::gpu::VertexBuffer::release() {
    if (m_data) {
        engine::memory::fast::alignedFree(m_data);
        m_data = nullptr;
    }
    m_count    = 0;
    m_capacity = 0;
    m_handle   = InvalidBuffer;
}

void engine::gpu::VertexBuffer::ensureCapacity(size_t needed) {
    if (needed <= m_capacity) return;
    size_t newCap = m_capacity ? m_capacity : 64;
    while (newCap < needed) newCap *= 2;

    void* newBuf = engine::memory::fast::alignedAlloc(newCap * m_stride, 64);
    if (!newBuf) throw std::bad_alloc();

    if (m_data && m_count > 0) {
        std::memcpy(newBuf, m_data, m_count * m_stride);
    }
    engine::memory::fast::alignedFree(m_data);
    m_data     = newBuf;
    m_capacity = newCap;
}

engine::gpu::VertexBuffer::VertexBuffer(VertexBuffer&& o) noexcept
    : m_handle(o.m_handle), m_stride(o.m_stride), m_count(o.m_count),
      m_data(o.m_data), m_capacity(o.m_capacity), m_dirty(o.m_dirty)
{ o.m_handle=InvalidBuffer; o.m_data=nullptr; o.m_count=0; o.m_capacity=0; }

engine::gpu::VertexBuffer& engine::gpu::VertexBuffer::operator=(VertexBuffer&& o) noexcept {
    if (this != &o) {
        release();
        m_handle=o.m_handle; m_stride=o.m_stride; m_count=o.m_count;
        m_data=o.m_data; m_capacity=o.m_capacity; m_dirty=o.m_dirty;
        o.m_handle=InvalidBuffer; o.m_data=nullptr; o.m_count=0; o.m_capacity=0;
    }
    return *this;
}


// ═══════════════════════════════════════════════
// IndexBuffer
// ═══════════════════════════════════════════════
void engine::gpu::IndexBuffer::create(IndexFormat fmt, size_t initialCapacity) {
    release();
    m_format = fmt;
    m_count  = 0;
    m_handle = s_nextHandle++;
    if (initialCapacity > 0) {
        m_data.resize(initialCapacity * indexSize());
    }
}

void engine::gpu::IndexBuffer::upload(const uint16_t* indices, size_t count) {
    m_format = IndexFormat::U16;
    m_count  = count;
    m_data.resize(count * 2);
    std::memcpy(m_data.data(), indices, count * 2);
}

void engine::gpu::IndexBuffer::upload(const uint32_t* indices, size_t count) {
    m_format = IndexFormat::U32;
    m_count  = count;
    m_data.resize(count * 4);
    std::memcpy(m_data.data(), indices, count * 4);
}

void engine::gpu::IndexBuffer::release() {
    m_data.clear();
    m_data.shrink_to_fit();
    m_count = 0;
    m_handle = InvalidBuffer;
}

engine::gpu::IndexBuffer::IndexBuffer(IndexBuffer&& o) noexcept
    : m_handle(o.m_handle), m_count(o.m_count), m_format(o.m_format), m_data(std::move(o.m_data))
{ o.m_handle=InvalidBuffer; o.m_count=0; }

engine::gpu::IndexBuffer& engine::gpu::IndexBuffer::operator=(IndexBuffer&& o) noexcept {
    if (this != &o) {
        release();
        m_handle=o.m_handle; m_count=o.m_count; m_format=o.m_format; m_data=std::move(o.m_data);
        o.m_handle=InvalidBuffer; o.m_count=0;
    }
    return *this;
}


// ═══════════════════════════════════════════════
// Mesh
// ═══════════════════════════════════════════════
void engine::gpu::Mesh::init(const MeshDescriptor& desc) {
    m_desc = desc;

    size_t stride = 12; // default
    switch (desc.vertexFormat) {
        case VertexFormatType::Position:              stride = VertexPosition::Stride;            break;
        case VertexFormatType::PositionNormal:        stride = VertexPositionNormal::Stride;      break;
        case VertexFormatType::PositionNormalUV:      stride = VertexPositionNormalUV::Stride;    break;
        case VertexFormatType::PositionColor:         stride = VertexPositionColor::Stride;       break;
        case VertexFormatType::PositionNormalUVTangent: stride = VertexPBR::Stride;               break;
    }
    m_vertexBuffer.create(stride);
    m_indexBuffer.create(IndexFormat::U32);
}

void engine::gpu::Mesh::computeBounds() {
    if (m_vertexBuffer.empty()) return;

    const char* base = static_cast<const char*>(m_vertexBuffer.data());
    size_t stride    = m_vertexBuffer.stride();
    size_t count     = m_vertexBuffer.count();

    math::fast::Vec3 bMin{1e30f, 1e30f, 1e30f};
    math::fast::Vec3 bMax{-1e30f, -1e30f, -1e30f};

    for (size_t i = 0; i < count; ++i) {
        // Position is always the first field
        const math::fast::Vec3& p = *reinterpret_cast<const math::fast::Vec3*>(base + i * stride);
        bMin.x = std::min(bMin.x, p.x);
        bMin.y = std::min(bMin.y, p.y);
        bMin.z = std::min(bMin.z, p.z);
        bMax.x = std::max(bMax.x, p.x);
        bMax.y = std::max(bMax.y, p.y);
        bMax.z = std::max(bMax.z, p.z);
    }
    m_desc.boundsMin = bMin;
    m_desc.boundsMax = bMax;
}


// ═══════════════════════════════════════════════
// VertexManager
// ═══════════════════════════════════════════════
engine::gpu::VertexManager& engine::gpu::VertexManager::instance() {
    static VertexManager inst;
    return inst;
}

engine::gpu::MeshHandle engine::gpu::VertexManager::createMesh(const MeshDescriptor& desc) {
    MeshHandle h = m_nextHandle++;
    auto& mesh = m_meshes[h];
    mesh.init(desc);
    return h;
}

void engine::gpu::VertexManager::destroyMesh(MeshHandle handle) {
    m_meshes.erase(handle);
}

engine::gpu::Mesh* engine::gpu::VertexManager::getMesh(MeshHandle handle) {
    auto it = m_meshes.find(handle);
    return (it != m_meshes.end()) ? &it->second : nullptr;
}

size_t engine::gpu::VertexManager::totalVertexCount() const {
    size_t total = 0;
    for (auto& [h, m] : m_meshes) total += m.vertexBuffer().count();
    return total;
}

size_t engine::gpu::VertexManager::totalIndexCount() const {
    size_t total = 0;
    for (auto& [h, m] : m_meshes) total += m.indexBuffer().count();
    return total;
}

size_t engine::gpu::VertexManager::totalByteSize() const {
    size_t total = 0;
    for (auto& [h, m] : m_meshes) {
        total += m.vertexBuffer().byteSize();
        total += m.indexBuffer().byteSize();
    }
    return total;
}
