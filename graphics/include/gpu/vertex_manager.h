#pragma once

#include "math/simd_math.h"
#include "memory/allocator.h"
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <numeric>

namespace engine {
namespace gpu {

// ═══════════════════════════════════════════════
// Vertex Formats
//
// Tightly packed structs.  Each layout is designed so that
// the GPU can consume it with minimal padding / wasted bandwidth.
// ═══════════════════════════════════════════════
enum class VertexFormatType : uint8_t {
    Position,          // Vec3                      (16 bytes)
    PositionNormal,    // Vec3 + Vec3               (32 bytes)
    PositionNormalUV,  // Vec3 + Vec3 + Vec2        (48 bytes)  ← standard mesh vertex
    PositionColor,     // Vec3 + Vec4               (32 bytes)
    PositionNormalUVTangent // Vec3 + Vec3 + Vec2 + Vec3 (64 bytes) ← PBR vertex
};

// ── Position only (debug / wireframe) ──
struct VertexPosition {
    math::fast::Vec3 pos;
    static constexpr size_t Stride = sizeof(math::fast::Vec3);  // 12 bytes
};

// ── Position + Normal ──
struct VertexPositionNormal {
    math::fast::Vec3 pos;
    math::fast::Vec3 normal;
    static constexpr size_t Stride = 32;
};

// ── Full mesh vertex ──
struct VertexPositionNormalUV {
    math::fast::Vec3 pos;
    math::fast::Vec3 normal;
    math::fast::Vec2 uv;
    static constexpr size_t Stride = 48;
};

// ── Colored vertex (particles, debug) ──
struct VertexPositionColor {
    math::fast::Vec3 pos;
    math::fast::Vec4 color;
    static constexpr size_t Stride = 32;
};

// ── PBR vertex with tangent ──
struct VertexPBR {
    math::fast::Vec3 pos;
    math::fast::Vec3 normal;
    math::fast::Vec2 uv;
    math::fast::Vec3 tangent;
    static constexpr size_t Stride = 64;
};


// ═══════════════════════════════════════════════
// Buffer handle (opaque ID for GPU-side buffers)
// ═══════════════════════════════════════════════
using BufferHandle = uint32_t;
constexpr BufferHandle InvalidBuffer = 0;


// ═══════════════════════════════════════════════
// VertexBuffer
//
// CPU-side vertex buffer with aligned backing store.
// Supports staged uploads and direct write.
// ═══════════════════════════════════════════════
class VertexBuffer {
public:
    VertexBuffer() : m_handle(InvalidBuffer), m_stride(0), m_count(0), m_data(nullptr), m_capacity(0) {}
    ~VertexBuffer() { release(); }

    // Non-copyable, movable
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    VertexBuffer(VertexBuffer&& o) noexcept;
    VertexBuffer& operator=(VertexBuffer&& o) noexcept;

    // Initialize with a stride and optional initial capacity
    void create(size_t stride, size_t initialCapacity = 0);

    // Upload raw bytes
    void upload(const void* data, size_t count);

    // Typed upload
    template<typename V>
    void upload(const V* vertices, size_t count) {
        upload(static_cast<const void*>(vertices), count);
    }

    // Append data
    void append(const void* data, size_t byteCount);

    // Direct pointer access (for in-place construction)
    void* mapRange(size_t startIndex, size_t count);

    // Finalize — mark buffer as ready for GPU consumption
    void finalize();

    // Release GPU + CPU resources
    void release();

    // Accessors
    BufferHandle handle()   const { return m_handle; }
    size_t       stride()   const { return m_stride; }
    size_t       count()    const { return m_count; }
    size_t       byteSize() const { return m_count * m_stride; }
    bool         empty()    const { return m_count == 0; }
    const void*  data()     const { return m_data; }
    bool         dirty()    const { return m_dirty; }

private:
    void ensureCapacity(size_t needed);

    BufferHandle m_handle;
    size_t       m_stride;
    size_t       m_count;
    void*        m_data;
    size_t       m_capacity;
    bool         m_dirty = false;

    static BufferHandle s_nextHandle;
};


// ═══════════════════════════════════════════════
// IndexBuffer  (uint16 or uint32 indices)
// ═══════════════════════════════════════════════
enum class IndexFormat : uint8_t { U16, U32 };

class IndexBuffer {
public:
    IndexBuffer() : m_handle(InvalidBuffer), m_count(0), m_format(IndexFormat::U32) {}
    ~IndexBuffer() { release(); }

    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;
    IndexBuffer(IndexBuffer&& o) noexcept;
    IndexBuffer& operator=(IndexBuffer&& o) noexcept;

    void create(IndexFormat fmt, size_t initialCapacity = 0);
    void upload(const uint16_t* indices, size_t count);
    void upload(const uint32_t* indices, size_t count);
    void release();

    BufferHandle handle()      const { return m_handle; }
    size_t       count()       const { return m_count; }
    IndexFormat  format()      const { return m_format; }
    size_t       indexSize()   const { return m_format == IndexFormat::U16 ? 2 : 4; }
    size_t       byteSize()    const { return m_count * indexSize(); }
    const void*  data()        const { return m_data.data(); }

private:
    BufferHandle       m_handle;
    size_t             m_count;
    IndexFormat        m_format;
    std::vector<char>  m_data;

    static BufferHandle s_nextHandle;
};


// ═══════════════════════════════════════════════
// Mesh  — combines a VertexBuffer + IndexBuffer + metadata
// ═══════════════════════════════════════════════
enum class PrimitiveType : uint8_t {
    Triangles, Lines, Points
};

struct MeshDescriptor {
    std::string        name;
    VertexFormatType   vertexFormat;
    PrimitiveType      primitiveType = PrimitiveType::Triangles;
    math::fast::Vec3         boundsMin;
    math::fast::Vec3         boundsMax;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    void init(const MeshDescriptor& desc);

    VertexBuffer&        vertexBuffer()        { return m_vertexBuffer; }
    const VertexBuffer&  vertexBuffer()  const { return m_vertexBuffer; }
    IndexBuffer&         indexBuffer()         { return m_indexBuffer;  }
    const IndexBuffer&   indexBuffer()   const { return m_indexBuffer;  }
    const MeshDescriptor& descriptor() const { return m_desc; }

    // Compute AABB from vertex data
    void computeBounds();

    bool valid() const { return !m_vertexBuffer.empty(); }

private:
    MeshDescriptor m_desc;
    VertexBuffer   m_vertexBuffer;
    IndexBuffer    m_indexBuffer;
};


// ═══════════════════════════════════════════════
// VertexManager  — the central coordinator
//
// Owns all live meshes.  Provides batch-upload, and
// tracks dirty buffers for the render loop.
// ═══════════════════════════════════════════════
using MeshHandle = uint32_t;
constexpr MeshHandle InvalidMesh = 0;

class VertexManager {
public:
    static VertexManager& instance();

    // Create / destroy meshes
    MeshHandle createMesh(const MeshDescriptor& desc);
    void       destroyMesh(MeshHandle handle);
    Mesh*      getMesh(MeshHandle handle);

    // Stats
    size_t meshCount()         const { return m_meshes.size(); }
    size_t totalVertexCount()  const;
    size_t totalIndexCount()   const;
    size_t totalByteSize()     const;

private:
    VertexManager() = default;

    std::unordered_map<MeshHandle, Mesh> m_meshes;
    MeshHandle m_nextHandle = 1;
};

} // namespace gpu
} // namespace engine
