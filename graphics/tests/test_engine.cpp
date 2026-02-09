#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <numeric>

#include "math/simd_math.h"
#include "memory/allocator.h"
#include "gpu/vertex_manager.h"
#include "render/pipeline.h"
#include "profiler/profiler.h"

// Use the fast (optimized) namespaces
using namespace engine::math::fast;
using namespace engine::memory::fast;
using namespace engine::gpu;
using namespace engine::render::fast;
using namespace engine::profiler;

// ═══════════════════════════════════════════════
// Test Harness
// ═══════════════════════════════════════════════
static int s_passed = 0, s_failed = 0;

#define TEST(name) \
    std::cout << "  " << name << " ... "; \
    std::cout.flush();

#define PASS() \
    std::cout << "\033[32m✓ PASS\033[0m\n"; ++s_passed;

#define FAIL(msg) \
    std::cout << "\033[31m✗ FAIL\033[0m — " << msg << "\n"; ++s_failed;

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::fabs((a)-(b)) > (tol)) { FAIL(msg); return; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { FAIL(msg); return; }

using Clock = std::chrono::high_resolution_clock;
static double elapsedMs(Clock::time_point s, Clock::time_point e) {
    return std::chrono::duration<double, std::milli>(e-s).count();
}


// ═══════════════════════════════════════════════
// 1. SIMD Math Tests
// ═══════════════════════════════════════════════
static void testVec2() {
    TEST("Vec2 basic arithmetic");
    Vec2 a(1,2), b(3,4);
    Vec2 c = a + b;
    ASSERT_NEAR(c.x, 4.f, 1e-6f, "add.x");
    ASSERT_NEAR(c.y, 6.f, 1e-6f, "add.y");
    Vec2 d = a - b;
    ASSERT_NEAR(d.x, -2.f, 1e-6f, "sub.x");
    Vec2 e = a * 2.f;
    ASSERT_NEAR(e.x, 2.f, 1e-6f, "mul.x");
    ASSERT_NEAR(e.y, 4.f, 1e-6f, "mul.y");
    ASSERT_NEAR(a.dot(b), 11.f, 1e-6f, "dot");
    ASSERT_NEAR(a.length(), std::sqrt(5.f), 1e-6f, "len");
    Vec2 n = a.normalized();
    ASSERT_NEAR(n.length(), 1.f, 1e-5f, "normalized length");
    ASSERT_TRUE(sizeof(Vec2) == 16, "sizeof Vec2 == 16");
    ASSERT_TRUE(alignof(Vec2) == 16, "alignof Vec2 == 16");
    ASSERT_TRUE((reinterpret_cast<uintptr_t>(&a) % 16) == 0, "Vec2 instance aligned");
    PASS();
}

static void testVec3() {
    TEST("Vec3 basic arithmetic");
    Vec3 a(1,2,3), b(4,5,6);
    Vec3 c = a + b;
    ASSERT_NEAR(c.x, 5.f, 1e-6f, "add.x");
    ASSERT_NEAR(c.y, 7.f, 1e-6f, "add.y");
    ASSERT_NEAR(c.z, 9.f, 1e-6f, "add.z");
    Vec3 d = a - b;
    ASSERT_NEAR(d.x,-3.f, 1e-6f, "sub.x");
    Vec3 e = a * 2.f;
    ASSERT_NEAR(e.x, 2.f, 1e-6f, "mul.x");
    ASSERT_NEAR(e.z, 6.f, 1e-6f, "mul.z");
    PASS();
}

static void testVec3Dot() {
    TEST("Vec3 dot & cross");
    Vec3 a(1,0,0), b(0,1,0);
    ASSERT_NEAR(a.dot(b), 0.f, 1e-6f, "dot ortho");
    ASSERT_NEAR(a.dot(a), 1.f, 1e-6f, "dot self");
    Vec3 c = a.cross(b);
    ASSERT_NEAR(c.x, 0.f, 1e-6f, "cross.x");
    ASSERT_NEAR(c.y, 0.f, 1e-6f, "cross.y");
    ASSERT_NEAR(c.z, 1.f, 1e-6f, "cross.z");
    PASS();
}

static void testVec3Normalize() {
    TEST("Vec3 normalize");
    Vec3 v(3,4,0);
    Vec3 n = v.normalized();
    ASSERT_NEAR(n.length(), 1.f, 1e-4f, "unit length");  // Relaxed tolerance for fast math
    ASSERT_NEAR(n.x, 0.6f, 1e-4f, "n.x");
    ASSERT_NEAR(n.y, 0.8f, 1e-4f, "n.y");
    PASS();
}

static void testMat4Identity() {
    TEST("Mat4 identity");
    Mat4 I = Mat4::identity();
    Vec3 p(1,2,3);
    Vec3 r = I.transformPoint(p);
    ASSERT_NEAR(r.x, 1.f, 1e-6f, "id.x");
    ASSERT_NEAR(r.y, 2.f, 1e-6f, "id.y");
    ASSERT_NEAR(r.z, 3.f, 1e-6f, "id.z");
    PASS();
}

static void testMat4Translate() {
    TEST("Mat4 translate");
    Mat4 T = Mat4::translate(Vec3(10, 20, 30));
    Vec3 p(1,2,3);
    Vec3 r = T.transformPoint(p);
    ASSERT_NEAR(r.x, 11.f, 1e-5f, "t.x");
    ASSERT_NEAR(r.y, 22.f, 1e-5f, "t.y");
    ASSERT_NEAR(r.z, 33.f, 1e-5f, "t.z");
    // Direction should NOT be affected by translation
    Vec3 d = T.transformDir(Vec3(1,0,0));
    ASSERT_NEAR(d.x, 1.f, 1e-5f, "dir.x");
    ASSERT_NEAR(d.y, 0.f, 1e-5f, "dir.y");
    PASS();
}

static void testMat4RotateY() {
    TEST("Mat4 rotateY 90°");
    Mat4 R = Mat4::rotateY(radians(90.f));
    Vec3 p(1,0,0);
    Vec3 r = R.transformPoint(p);
    ASSERT_NEAR(r.x,  0.f, 1e-4f, "ry.x");  // Relaxed for fast sin/cos
    ASSERT_NEAR(r.y,  0.f, 1e-4f, "ry.y");
    ASSERT_NEAR(r.z, -1.f, 1e-4f, "ry.z");
    PASS();
}

static void testMat4Multiply() {
    TEST("Mat4 multiply T*R");
    Mat4 T = Mat4::translate(Vec3(5,0,0));
    Mat4 R = Mat4::rotateY(radians(90.f));
    Mat4 TR = T * R;
    Vec3 p(1,0,0);
    Vec3 r = TR.transformPoint(p);
    // Rotate (1,0,0) by 90° Y → (0,0,-1), then translate +5 on X → (5,0,-1)
    ASSERT_NEAR(r.x, 5.f, 1e-3f, "TR.x");
    ASSERT_NEAR(r.y, 0.f, 1e-3f, "TR.y");
    ASSERT_NEAR(r.z,-1.f, 1e-3f, "TR.z");
    PASS();
}

static void testMat4Scale() {
    TEST("Mat4 scale");
    Mat4 S = Mat4::scale(Vec3(2,3,4));
    Vec3 p(1,1,1);
    Vec3 r = S.transformPoint(p);
    ASSERT_NEAR(r.x, 2.f, 1e-6f, "s.x");
    ASSERT_NEAR(r.y, 3.f, 1e-6f, "s.y");
    ASSERT_NEAR(r.z, 4.f, 1e-6f, "s.z");
    PASS();
}

static void testMat4LookAt() {
    TEST("Mat4 lookAt");
    Mat4 V = Mat4::lookAt(Vec3(0,0,5), Vec3(0,0,0), Vec3(0,1,0));
    // A point at world origin should map to (0,0,-5) in view space
    Vec3 p = V.transformPoint(Vec3(0,0,0));
    ASSERT_NEAR(p.x,  0.f, 1e-4f, "la.x");
    ASSERT_NEAR(p.y,  0.f, 1e-4f, "la.y");
    ASSERT_NEAR(p.z, -5.f, 1e-4f, "la.z");
    PASS();
}

static void testMat4Inverse3x3() {
    TEST("Mat4 inverse3x3 (rotation)");
    Mat4 R = Mat4::rotateY(radians(37.f));
    Mat4 Ri = R.inverse3x3();
    Mat4 I = R * Ri;
    // Should be close to identity in the 3x3 block
    ASSERT_NEAR(I(0,0), 1.f, 1e-3f, "I00");
    ASSERT_NEAR(I(1,1), 1.f, 1e-3f, "I11");
    ASSERT_NEAR(I(2,2), 1.f, 1e-3f, "I22");
    ASSERT_NEAR(I(0,1), 0.f, 1e-3f, "I01");
    ASSERT_NEAR(I(1,0), 0.f, 1e-3f, "I10");
    PASS();
}


// ═══════════════════════════════════════════════
// 2. SIMD Batch Tests
// ═══════════════════════════════════════════════
static void testBatchTransform() {
    TEST("batchTransformPointsFast (N=1024)");
    const size_t N = 1024;
    alignas(64) static Vec3 input[1024];
    alignas(64) static Vec3 output[1024];

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.f, 100.f);
    for (size_t i = 0; i < N; ++i) {
        input[i] = Vec3(dist(rng), dist(rng), dist(rng));
    }

    Mat4 T = Mat4::translate(Vec3(1,2,3)) * Mat4::rotateY(radians(45.f)) * Mat4::scale(Vec3(2,2,2));

    batchTransformPointsFast(T, input, output, N);

    // Verify against scalar
    for (size_t i = 0; i < N; ++i) {
        Vec3 expected = T.transformPoint(input[i]);
        ASSERT_NEAR(output[i].x, expected.x, 1e-2f, "batch.x[" + std::to_string(i) + "]");
        ASSERT_NEAR(output[i].y, expected.y, 1e-2f, "batch.y");
        ASSERT_NEAR(output[i].z, expected.z, 1e-2f, "batch.z");
    }
    PASS();
}

static void testBatchMultiplyMat4() {
    TEST("batchMultiplyMat4Fast (N=512)");
    const size_t N = 512;
    alignas(64) static Mat4 parents[512];
    alignas(64) static Mat4 locals[512];
    alignas(64) static Mat4 results[512];

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> angleDist(-PI, PI);
    std::uniform_real_distribution<float> posDist(-50.f, 50.f);

    for (size_t i = 0; i < N; ++i) {
        parents[i] = Mat4::translate(Vec3(posDist(rng), posDist(rng), posDist(rng)))
                   * Mat4::rotateY(angleDist(rng));
        locals[i] = Mat4::translate(Vec3(posDist(rng)*0.1f, 0.f, 0.f))
                   * Mat4::scale(Vec3(1.f, 1.f, 1.f));
    }

    batchMultiplyMat4Fast(parents, locals, results, N);

    // Verify a sample
    for (size_t i = 0; i < N; i += 64) {
        Mat4 expected = parents[i] * locals[i];
        for (int c = 0; c < 4; ++c) {
            ASSERT_NEAR(results[i].col[c].x, expected.col[c].x, 1e-2f, "bmm col");
            ASSERT_NEAR(results[i].col[c].y, expected.col[c].y, 1e-2f, "bmm col");
            ASSERT_NEAR(results[i].col[c].z, expected.col[c].z, 1e-2f, "bmm col");
            ASSERT_NEAR(results[i].col[c].w, expected.col[c].w, 1e-2f, "bmm col");
        }
    }
    PASS();
}

static void testBatchDot3() {
    TEST("batchDot3Fast (N=2048)");
    const size_t N = 2048;
    alignas(64) static Vec3 a[2048];
    alignas(64) static Vec3 b[2048];
    alignas(64) static float out[2048];

    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (size_t i = 0; i < N; ++i) {
        a[i] = Vec3(dist(rng), dist(rng), dist(rng));
        b[i] = Vec3(dist(rng), dist(rng), dist(rng));
    }

    batchDot3Fast(a, b, out, N);

    for (size_t i = 0; i < N; ++i) {
        float expected = a[i].dot(b[i]);
        ASSERT_NEAR(out[i], expected, 1e-4f, "dot3[" + std::to_string(i) + "]");
    }
    PASS();
}


// ═══════════════════════════════════════════════
// 3. Memory Allocator Tests (Fast versions)
// ═══════════════════════════════════════════════
struct DummyObject {
    float data[4];
    int id;
};

static void testLockFreePool() {
    TEST("LockFreePool alloc/dealloc cycle");
    LockFreePool<DummyObject> pool(256);

    std::vector<DummyObject*> ptrs;
    ptrs.reserve(100);
    for (int i = 0; i < 100; ++i) {
        DummyObject* obj = pool.construct(DummyObject{{1.f,2.f,3.f,4.f}, i});
        ASSERT_TRUE(obj != nullptr, "alloc not null");
        ptrs.push_back(obj);
    }
    ASSERT_TRUE(pool.size() == 100, "pool size after alloc");

    // Verify data integrity
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(ptrs[i]->id == i, "pool data integrity");
        ASSERT_NEAR(ptrs[i]->data[0], 1.f, 1e-6f, "pool data[0]");
    }

    // Free even-indexed
    for (int i = 0; i < 100; i += 2) {
        pool.destroy(ptrs[i]);
    }
    ASSERT_TRUE(pool.size() == 50, "pool size after half-free");

    // Re-allocate from free list
    for (int i = 0; i < 50; ++i) {
        DummyObject* obj = pool.construct(DummyObject{{5.f,6.f,7.f,8.f}, 200+i});
        ASSERT_TRUE(obj != nullptr, "realloc from free list");
    }
    ASSERT_TRUE(pool.size() == 100, "pool size after re-alloc");
    PASS();
}

static void testThreadLocalArena() {
    TEST("ThreadLocalArena bump + reset");
    ThreadLocalArena arena(4096);
    ASSERT_TRUE(arena.used() == 0, "arena initial used");

    // Allocate a few things
    float* arr = arena.allocateArray<float>(100);
    ASSERT_TRUE(arr != nullptr, "arena array ptr");
    ASSERT_TRUE(arena.used() >= 400, "arena used after float[100]");

    int* iarr = arena.allocateArray<int>(50);
    ASSERT_TRUE(iarr != nullptr, "arena int array");

    size_t usedBefore = arena.used();

    // Reset — instant free
    arena.reset();
    ASSERT_TRUE(arena.used() == 0, "arena used after reset");
    ASSERT_TRUE(usedBefore > 0, "arena had data before reset");
    PASS();
}

static void testAlignedAlloc() {
    TEST("alignedAlloc 64-byte alignment");
    void* ptr = alignedAlloc(1024, 64);
    ASSERT_TRUE(ptr != nullptr, "aligned ptr not null");
    ASSERT_TRUE((reinterpret_cast<uintptr_t>(ptr) % 64) == 0, "64-byte aligned");
    alignedFree(ptr);
    PASS();
}


// ═══════════════════════════════════════════════
// 4. Vertex Buffer Tests (uses original engine::gpu)
// ═══════════════════════════════════════════════
static void testVertexBuffer() {
    TEST("VertexBuffer create/upload/append");
    VertexBuffer vb;
    vb.create(VertexPositionNormalUV::Stride, 64);

    // Build test data
    std::vector<VertexPositionNormalUV> verts(128);
    for (int i = 0; i < 128; ++i) {
        verts[i].pos = Vec3((float)i, 0.f, 0.f);
        verts[i].normal = Vec3(0.f, 1.f, 0.f);
        verts[i].uv = Vec2((float)i / 128.f, 0.f);
    }

    vb.upload(verts.data(), 128);
    ASSERT_TRUE(vb.count() == 128, "vb count");
    ASSERT_TRUE(vb.dirty(), "vb dirty after upload");
    ASSERT_TRUE(vb.byteSize() == 128 * VertexPositionNormalUV::Stride, "vb byteSize");

    // Append
    vb.append(verts.data(), 32 * VertexPositionNormalUV::Stride);
    ASSERT_TRUE(vb.count() == 160, "vb count after append");

    // Finalize clears dirty
    vb.finalize();
    ASSERT_TRUE(!vb.dirty(), "vb not dirty after finalize");

    // Verify first vertex
    const auto* first = reinterpret_cast<const VertexPositionNormalUV*>(vb.data());
    ASSERT_NEAR(first[0].pos.x, 0.f, 1e-6f, "first vert pos.x");
    ASSERT_NEAR(first[5].pos.x, 5.f, 1e-6f, "vert[5] pos.x");
    PASS();
}

static void testIndexBuffer() {
    TEST("IndexBuffer U32 upload");
    IndexBuffer ib;
    ib.create(IndexFormat::U32, 0);

    std::vector<uint32_t> indices(300);
    std::iota(indices.begin(), indices.end(), 0u);
    ib.upload(indices.data(), 300);

    ASSERT_TRUE(ib.count() == 300, "ib count");
    ASSERT_TRUE(ib.format() == IndexFormat::U32, "ib format");
    ASSERT_TRUE(ib.byteSize() == 1200, "ib byteSize");

    const uint32_t* data = reinterpret_cast<const uint32_t*>(ib.data());
    ASSERT_TRUE(data[0] == 0, "ib[0]");
    ASSERT_TRUE(data[299] == 299, "ib[299]");
    PASS();
}

static void testVertexManager() {
    TEST("VertexManager create/get/destroy");
    auto& vm = VertexManager::instance();
    size_t initialCount = vm.meshCount();

    MeshDescriptor desc;
    desc.name         = "TestCube";
    desc.vertexFormat = VertexFormatType::PositionNormalUV;
    engine::gpu::MeshHandle h = vm.createMesh(desc);
    ASSERT_TRUE(h != engine::gpu::InvalidMesh, "mesh handle valid");
    ASSERT_TRUE(vm.meshCount() == initialCount + 1, "mesh count +1");

    Mesh* mesh = vm.getMesh(h);
    ASSERT_TRUE(mesh != nullptr, "getMesh not null");
    ASSERT_TRUE(mesh->descriptor().name == "TestCube", "mesh name");

    vm.destroyMesh(h);
    ASSERT_TRUE(vm.meshCount() == initialCount, "mesh count after destroy");
    PASS();
}


// ═══════════════════════════════════════════════
// 5. Fast Pipeline Tests
// ═══════════════════════════════════════════════
static void testFastPipelineNodes() {
    TEST("FastRenderPipeline node creation");
    auto& pipeline = FastRenderPipeline::instance();
    
    NodeHandle node1 = pipeline.createNode();
    NodeHandle node2 = pipeline.createNode();
    
    ASSERT_TRUE(node1 != InvalidNode, "node1 valid");
    ASSERT_TRUE(node2 != InvalidNode, "node2 valid");
    ASSERT_TRUE(node1 != node2, "nodes are different");
    
    pipeline.setPosition(node1, Vec3(10, 20, 30));
    pipeline.setRotation(node1, Vec3(0, radians(45.f), 0));
    pipeline.setScale(node1, Vec3(2, 2, 2));
    pipeline.setMesh(node1, 1);  // Dummy mesh handle
    pipeline.setMaterial(node1, 1);  // Dummy material handle
    
    pipeline.destroyNode(node1);
    pipeline.destroyNode(node2);
    PASS();
}

static void testFastPipelineTransformUpdate() {
    TEST("FastRenderPipeline transform update");
    auto& pipeline = FastRenderPipeline::instance();
    
    // Create some nodes
    std::vector<NodeHandle> nodes;
    for (int i = 0; i < 100; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3((float)i, 0, 0));
        pipeline.setMesh(h, 1);
        nodes.push_back(h);
    }
    
    pipeline.setCamera(Vec3(0, 0, 100), Vec3(0, 0, 0), Vec3(0, 1, 0),
                       60.f, 1.333f, 0.1f, 500.f);
    
    // Update transforms
    pipeline.updateTransforms();
    
    const auto& stats = pipeline.stats();
    ASSERT_TRUE(stats.totalNodes.load() >= 100, "total nodes >= 100");
    ASSERT_TRUE(stats.transformUpdateUs.load() < 100000, "transform update < 100ms");
    
    for (auto h : nodes) {
        pipeline.destroyNode(h);
    }
    PASS();
}

static void testFastPipelineFrustumCull() {
    TEST("FastRenderPipeline frustum culling");
    auto& pipeline = FastRenderPipeline::instance();
    
    // Create nodes - some inside frustum, some outside
    std::vector<NodeHandle> nodes;
    
    // Nodes at origin (should be visible)
    for (int i = 0; i < 50; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3((float)(i % 10) * 2, 0, 0));
        pipeline.setMesh(h, 1);
        pipeline.setMaterial(h, 1);
        nodes.push_back(h);
    }
    
    // Nodes far away (should be culled)
    for (int i = 0; i < 50; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3(0, 0, -1000));  // Behind far plane
        pipeline.setMesh(h, 1);
        pipeline.setMaterial(h, 1);
        nodes.push_back(h);
    }
    
    pipeline.setCamera(Vec3(0, 0, 50), Vec3(0, 0, 0), Vec3(0, 1, 0),
                       60.f, 1.333f, 0.1f, 200.f);
    
    pipeline.updateTransforms();
    pipeline.frustumCull();
    
    const auto& stats = pipeline.stats();
    ASSERT_TRUE(stats.culledNodes.load() >= 1, "at least 1 node culled");
    ASSERT_TRUE(stats.visibleNodes.load() >= 1, "at least 1 node visible");
    ASSERT_TRUE(stats.frustumCullUs.load() < 100000, "frustum cull < 100ms");
    
    for (auto h : nodes) {
        pipeline.destroyNode(h);
    }
    PASS();
}

static void testFastPipelineFullFrame() {
    TEST("FastRenderPipeline full frame render");
    auto& pipeline = FastRenderPipeline::instance();
    
    // Create 1000 nodes
    std::vector<NodeHandle> nodes;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-50.f, 50.f);
    
    for (int i = 0; i < 1000; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3(posDist(rng), posDist(rng), posDist(rng)));
        pipeline.setMesh(h, 1);
        pipeline.setMaterial(h, 1);
        nodes.push_back(h);
    }
    
    pipeline.setCamera(Vec3(0, 0, 100), Vec3(0, 0, 0), Vec3(0, 1, 0),
                       60.f, 1.333f, 0.1f, 500.f);
    
    // Render full frame
    pipeline.renderFrame();
    
    const auto& stats = pipeline.stats();
    ASSERT_TRUE(stats.totalNodes.load() >= 1000, "total nodes >= 1000");
    
    // Total frame time should be reasonable (< 10ms for 1000 nodes)
    uint64_t totalUs = stats.transformUpdateUs.load() + 
                       stats.frustumCullUs.load() + 
                       stats.drawBuildUs.load();
    ASSERT_TRUE(totalUs < 10000, "full frame < 10ms");
    
    for (auto h : nodes) {
        pipeline.destroyNode(h);
    }
    PASS();
}


// ═══════════════════════════════════════════════
// 6. Profiler Tests
// ═══════════════════════════════════════════════
static void testProfiler() {
    TEST("Profiler section timing");
    Profiler& prof = Profiler::instance();
    prof.setEnabled(true);

    prof.beginSection("TestSection");
    // Burn ~1ms
    volatile float sum = 0;
    for (int i = 0; i < 500000; ++i) sum += 0.001f;
    prof.endSection("TestSection");

    const SectionStats* stats = prof.getStats("TestSection");
    ASSERT_TRUE(stats != nullptr, "stats exist");
    ASSERT_TRUE(stats->callCount >= 1, "called at least once");
    ASSERT_TRUE(stats->lastMs > 0.0, "lastMs > 0");
    PASS();
}

static void testScopedProfile() {
    TEST("ScopedProfile RAII");
    {
        ScopedProfile sp("ScopedTest");
        volatile float x = 0;
        for (int i = 0; i < 100000; ++i) x += 0.01f;
    }
    const SectionStats* stats = Profiler::instance().getStats("ScopedTest");
    ASSERT_TRUE(stats != nullptr, "scoped stats exist");
    ASSERT_TRUE(stats->lastMs > 0.0, "scoped lastMs > 0");
    PASS();
}


// ═══════════════════════════════════════════════
// 7. Performance Benchmarks
// ═══════════════════════════════════════════════
static void benchmarkBatchTransform() {
    TEST("BENCHMARK batchTransformPointsFast 100K");
    constexpr size_t N = 100'000;
    alignas(64) static Vec3 input[100000];
    alignas(64) static Vec3 output[100000];

    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1000.f, 1000.f);
    for (size_t i = 0; i < N; ++i)
        input[i] = Vec3(dist(rng), dist(rng), dist(rng));

    Mat4 M = Mat4::translate(Vec3(1,2,3)) * Mat4::rotateY(0.5f) * Mat4::scale(Vec3(2,1.5f,0.8f));

    // Warm up
    batchTransformPointsFast(M, input, output, N);

    // Benchmark: 100 iterations
    auto t0 = Clock::now();
    for (int iter = 0; iter < 100; ++iter) {
        batchTransformPointsFast(M, input, output, N);
    }
    auto t1 = Clock::now();
    double totalMs = elapsedMs(t0, t1);
    double perIter = totalMs / 100.0;

    std::cout << "\033[32m✓ PASS\033[0m  ("
              << std::fixed << std::setprecision(3) << perIter << " ms/iter, "
              << (N / perIter / 1000.0) << "M pts/s)\n";
    ++s_passed;
}

static void benchmarkBatchMat4Multiply() {
    TEST("BENCHMARK batchMultiplyMat4Fast 50K");
    constexpr size_t N = 50'000;
    alignas(64) static Mat4 parents[50000];
    alignas(64) static Mat4 locals[50000];
    alignas(64) static Mat4 results[50000];

    std::mt19937 rng(5678);
    std::uniform_real_distribution<float> dist(-10.f, 10.f);
    for (size_t i = 0; i < N; ++i) {
        parents[i] = Mat4::translate(Vec3(dist(rng), dist(rng), dist(rng))) * Mat4::rotateY(dist(rng));
        locals[i] = Mat4::translate(Vec3(dist(rng)*0.1f, 0.f, 0.f));
    }

    // Warm up
    batchMultiplyMat4Fast(parents, locals, results, N);

    auto t0 = Clock::now();
    for (int iter = 0; iter < 100; ++iter) {
        batchMultiplyMat4Fast(parents, locals, results, N);
    }
    auto t1 = Clock::now();
    double totalMs = elapsedMs(t0, t1);
    double perIter = totalMs / 100.0;

    std::cout << "\033[32m✓ PASS\033[0m  ("
              << std::fixed << std::setprecision(3) << perIter << " ms/iter, "
              << (N / perIter / 1000.0) << "M mults/s)\n";
    ++s_passed;
}

static void benchmarkLockFreePool() {
    TEST("BENCHMARK LockFreePool 100K alloc/free cycles");
    LockFreePool<DummyObject> pool(2048);

    auto t0 = Clock::now();
    for (int cycle = 0; cycle < 100; ++cycle) {
        std::vector<DummyObject*> ptrs;
        ptrs.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            ptrs.push_back(pool.construct(DummyObject{{1,2,3,4}, i}));
        }
        for (auto p : ptrs) pool.destroy(p);
    }
    auto t1 = Clock::now();
    double ms = elapsedMs(t0, t1);

    std::cout << "\033[32m✓ PASS\033[0m  ("
              << std::fixed << std::setprecision(3) << ms << " ms for 100K cycles, "
              << (100000.0 / ms / 1000.0) << "M allocs/s)\n";
    ++s_passed;
}

static void benchmarkFastPipeline() {
    TEST("BENCHMARK FastRenderPipeline 10K nodes full frame");
    auto& pipeline = FastRenderPipeline::instance();
    
    // Create 10K nodes
    std::vector<NodeHandle> nodes;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-100.f, 100.f);
    
    for (int i = 0; i < 10000; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3(posDist(rng), posDist(rng), posDist(rng)));
        pipeline.setMesh(h, 1);
        pipeline.setMaterial(h, 1);
        nodes.push_back(h);
    }
    
    pipeline.setCamera(Vec3(0, 0, 200), Vec3(0, 0, 0), Vec3(0, 1, 0),
                       60.f, 1.333f, 0.1f, 500.f);
    
    // Warm up
    pipeline.renderFrame();
    
    // Benchmark
    auto t0 = Clock::now();
    for (int iter = 0; iter < 100; ++iter) {
        // Mark all dirty
        for (auto h : nodes) {
            pipeline.setPosition(h, Vec3(posDist(rng), posDist(rng), posDist(rng)));
        }
        pipeline.renderFrame();
    }
    auto t1 = Clock::now();
    double totalMs = elapsedMs(t0, t1);
    double perIter = totalMs / 100.0;
    
    const auto& stats = pipeline.stats();
    
    std::cout << "\033[32m✓ PASS\033[0m  ("
              << std::fixed << std::setprecision(3) << perIter << " ms/frame, "
              << "transform:" << stats.transformUpdateUs.load() << "µs, "
              << "cull:" << stats.frustumCullUs.load() << "µs, "
              << "draw:" << stats.drawBuildUs.load() << "µs)\n";
    ++s_passed;
    
    for (auto h : nodes) {
        pipeline.destroyNode(h);
    }
}


// ═══════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════
int main() {
    std::cout << "\n\033[1m╔══════════════════════════════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1m║   OPTIMIZED GRAPHICS ENGINE — TEST SUITE (FAST NAMESPACES)      ║\033[0m\n";
    std::cout << "\033[1m╚══════════════════════════════════════════════════════════════════╝\033[0m\n\n";

    // SIMD detection
    std::cout << "SIMD Support: ";
#if defined(ENGINE_USE_AVX512)
    std::cout << "\033[32mAVX-512\033[0m\n";
#elif defined(ENGINE_USE_AVX2)
    std::cout << "\033[32mAVX2 + FMA\033[0m\n";
#elif defined(ENGINE_USE_SSE4)
    std::cout << "\033[32mSSE4.1\033[0m\n";
#elif defined(ENGINE_USE_SSE2)
    std::cout << "\033[33mSSE2\033[0m\n";
#else
    std::cout << "\033[31mScalar\033[0m\n";
#endif
    std::cout << "\n";

    // ── Math ──
    std::cout << "\033[1m[MATH]\033[0m  Core vector & matrix operations\n";
    testVec2();
    testVec3();
    testVec3Dot();
    testVec3Normalize();
    testMat4Identity();
    testMat4Translate();
    testMat4RotateY();
    testMat4Multiply();
    testMat4Scale();
    testMat4LookAt();
    testMat4Inverse3x3();

    // ── SIMD Batch ──
    std::cout << "\n\033[1m[SIMD]\033[0m  Batch-accelerated operations\n";
    testBatchTransform();
    testBatchMultiplyMat4();
    testBatchDot3();

    // ── Memory ──
    std::cout << "\n\033[1m[MEMORY]\033[0m  Lock-free allocators\n";
    testLockFreePool();
    testThreadLocalArena();
    testAlignedAlloc();

    // ── GPU / Vertex ──
    std::cout << "\n\033[1m[GPU]\033[0m  Vertex & index buffers\n";
    testVertexBuffer();
    testIndexBuffer();
    testVertexManager();

    // ── Fast Pipeline ──
    std::cout << "\n\033[1m[PIPELINE]\033[0m  Fast render pipeline\n";
    testFastPipelineNodes();
    testFastPipelineTransformUpdate();
    testFastPipelineFrustumCull();
    testFastPipelineFullFrame();

    // ── Profiler ──
    std::cout << "\n\033[1m[PROFILER]\033[0m  Timing & RAII markers\n";
    testProfiler();
    testScopedProfile();

    // ── Benchmarks ──
    std::cout << "\n\033[1m[BENCHMARKS]\033[0m  Performance measurements\n";
    benchmarkBatchTransform();
    benchmarkBatchMat4Multiply();
    benchmarkLockFreePool();
    benchmarkFastPipeline();

    // ── Profiler Report ──
    Profiler::instance().printReport();

    // ── Summary ──
    std::cout << "\033[1m╔══════════════════════════════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1m║  RESULTS: \033[0m"
              << "\033[32m" << s_passed << " passed\033[0m  "
              << (s_failed ? "\033[31m" : "\033[32m") << s_failed << " failed\033[0m"
              << "                                       \033[1m║\033[0m\n";
    std::cout << "\033[1m╚══════════════════════════════════════════════════════════════════╝\033[0m\n\n";

    return s_failed > 0 ? 1 : 0;
}
