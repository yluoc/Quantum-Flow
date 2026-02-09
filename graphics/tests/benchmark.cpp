#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#include <cassert>

#include "math/simd_math.h"
#include "memory/allocator.h"
#include "render/pipeline.h"

using namespace engine::math::fast;
using namespace engine::memory::fast;
using namespace engine::render::fast;

// ═══════════════════════════════════════════════
// Benchmark Harness
// ═══════════════════════════════════════════════
static int s_passed = 0, s_failed = 0;

#define BENCHMARK(name) \
    std::cout << "  " << std::left << std::setw(45) << name << std::flush;

#define RESULT(timeUs, throughput) \
    std::cout << "\033[32m" << std::right << std::setw(8) << std::fixed \
              << std::setprecision(1) << timeUs << " µs\033[0m  " \
              << std::setw(12) << throughput << "\n"; ++s_passed;

#define FAIL(msg) \
    std::cout << "\033[31mFAIL\033[0m — " << msg << "\n"; ++s_failed;

using Clock = std::chrono::high_resolution_clock;

// Prevent compiler from optimizing away a value
template<typename T>
inline void doNotOptimize(T const& value) {
    asm volatile("" : : "g"(value) : "memory");
}

// Force compiler to assume memory pointed to by p may be read externally
inline void escapePointer(void* p) {
    asm volatile("" : : "g"(p) : "memory");
}

// Prevent compiler from reordering memory accesses across this barrier
inline void clobberMemory() {
    asm volatile("" : : : "memory");
}

template<typename F>
double measureUs(F&& func, int iterations = 100) {
    // Warmup
    for (int i = 0; i < 10; ++i) func();

    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = Clock::now();

    return std::chrono::duration<double, std::micro>(end - start).count() / iterations;
}


// ═══════════════════════════════════════════════
// SIMD Math Benchmarks
// ═══════════════════════════════════════════════
void benchmarkVec3Operations() {
    constexpr size_t N = 10000;
    alignas(64) static Vec3 a[N], b[N], c[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.f, 100.f);
    
    for (size_t i = 0; i < N; ++i) {
        a[i] = Vec3(dist(rng), dist(rng), dist(rng));
        b[i] = Vec3(dist(rng), dist(rng), dist(rng));
    }
    
    BENCHMARK("Vec3 add (10K)");
    double us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            c[i] = a[i] + b[i];
        }
        escapePointer(c);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    BENCHMARK("Vec3 dot (10K)");
    static float dots[N];
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            dots[i] = a[i].dot(b[i]);
        }
        escapePointer(dots);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    BENCHMARK("Vec3 cross (10K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            c[i] = a[i].cross(b[i]);
        }
        escapePointer(c);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    BENCHMARK("Vec3 normalize (10K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            c[i] = a[i].normalized();
        }
        escapePointer(c);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");
}

void benchmarkMat4Operations() {
    constexpr size_t N = 1000;
    alignas(64) static Mat4 a[N], b[N], c[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-10.f, 10.f);
    
    for (size_t i = 0; i < N; ++i) {
        a[i] = Mat4::translate(Vec3(dist(rng), dist(rng), dist(rng)))
             * Mat4::rotateY(dist(rng));
        b[i] = Mat4::scale(Vec3(dist(rng), dist(rng), dist(rng)));
    }
    
    BENCHMARK("Mat4 multiply (1K)");
    double us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            c[i] = a[i] * b[i];
        }
        escapePointer(c);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us * 1000) : 0) + "K muls/s");

    BENCHMARK("Mat4 transpose (1K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            c[i] = a[i].transpose();
        }
        escapePointer(c);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us * 1000) : 0) + "K ops/s");
}

void benchmarkBatchTransform() {
    constexpr size_t N = 100000;
    alignas(64) static Vec3 input[N], output[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1000.f, 1000.f);
    
    for (size_t i = 0; i < N; ++i) {
        input[i] = Vec3(dist(rng), dist(rng), dist(rng));
    }
    
    Mat4 transform = Mat4::translate(Vec3(1, 2, 3))
                   * Mat4::rotateY(0.5f)
                   * Mat4::scale(Vec3(2, 2, 2));
    
    BENCHMARK("batchTransformPoints (100K)");
    double us = measureUs([&]() {
        batchTransformPointsFast(transform, input, output, N);
    });
    RESULT(us, std::to_string(int(N / us)) + "M pts/s");
}

void benchmarkBatchMatrixMultiply() {
    constexpr size_t N = 50000;
    alignas(64) static Mat4 parents[N], locals[N], results[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-10.f, 10.f);
    
    for (size_t i = 0; i < N; ++i) {
        parents[i] = Mat4::translate(Vec3(dist(rng), dist(rng), dist(rng)));
        locals[i] = Mat4::rotateY(dist(rng));
    }
    
    BENCHMARK("batchMultiplyMat4 (50K)");
    double us = measureUs([&]() {
        batchMultiplyMat4Fast(parents, locals, results, N);
    });
    RESULT(us, std::to_string(int(N / us * 1000)) + "K muls/s");
}

void benchmarkBatchDot() {
    constexpr size_t N = 100000;
    alignas(64) static Vec3 a[N], b[N];
    alignas(64) static float results[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    
    for (size_t i = 0; i < N; ++i) {
        a[i] = Vec3(dist(rng), dist(rng), dist(rng));
        b[i] = Vec3(dist(rng), dist(rng), dist(rng));
    }
    
    BENCHMARK("batchDot3 (100K)");
    double us = measureUs([&]() {
        batchDot3Fast(a, b, results, N);
    });
    RESULT(us, std::to_string(int(N / us)) + "M dots/s");
}


// ═══════════════════════════════════════════════
// Memory Allocator Benchmarks
// ═══════════════════════════════════════════════
struct TestObject {
    float data[8];
    int id;
};

void benchmarkLockFreePool() {
    BENCHMARK("LockFreePool alloc/free (10K cycles)");
    
    LockFreePool<TestObject> pool(16384);
    
    double us = measureUs([&]() {
        std::vector<TestObject*> ptrs;
        ptrs.reserve(100);
        
        for (int i = 0; i < 100; ++i) {
            ptrs.push_back(pool.allocate());
        }
        for (auto* p : ptrs) {
            pool.deallocate(p);
        }
    }, 100);
    
    RESULT(us, std::to_string(int(200.0 / us * 1000)) + "K allocs/s");
}

void benchmarkThreadLocalArena() {
    BENCHMARK("ThreadLocalArena bump (100K allocs)");
    
    ThreadLocalArena arena(16 * 1024 * 1024);  // 16MB
    
    double us = measureUs([&]() {
        arena.reset();
        for (int i = 0; i < 100000; ++i) {
            void* p = arena.allocate(64, 8);
            (void)p;
        }
    });
    
    RESULT(us, std::to_string(int(100000.0 / us)) + "M allocs/s");
}

void benchmarkSlabAllocator() {
    BENCHMARK("SlabAllocator mixed sizes (10K)");
    
    SlabAllocator slab;
    std::vector<std::pair<void*, size_t>> allocs;
    allocs.reserve(10000);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> sizeDist(16, 512);
    
    std::vector<size_t> sizes;
    sizes.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        sizes.push_back(sizeDist(rng));
    }
    
    double us = measureUs([&]() {
        allocs.clear();
        for (size_t sz : sizes) {
            void* p = slab.allocate(sz);
            allocs.emplace_back(p, sz);
        }
        for (auto& [p, sz] : allocs) {
            slab.deallocate(p, sz);
        }
    }, 10);
    
    RESULT(us, std::to_string(int(20000.0 / us * 1000)) + "K ops/s");
}


// ═══════════════════════════════════════════════
// Render Pipeline Benchmarks
// ═══════════════════════════════════════════════
void benchmarkTransformUpdate() {
    auto& pipeline = FastRenderPipeline::instance();
    
    // Create 10K nodes
    std::vector<NodeHandle> nodes;
    nodes.reserve(10000);
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-100.f, 100.f);
    std::uniform_real_distribution<float> rotDist(-PI, PI);
    
    for (int i = 0; i < 10000; ++i) {
        NodeHandle h = pipeline.createNode();
        pipeline.setPosition(h, Vec3(posDist(rng), posDist(rng), posDist(rng)));
        pipeline.setRotation(h, Vec3(rotDist(rng), rotDist(rng), rotDist(rng)));
        pipeline.setMesh(h, 1);  // Dummy mesh
        pipeline.setMaterial(h, 1);
        nodes.push_back(h);
    }
    
    pipeline.setCamera(Vec3(0, 0, 500), Vec3(0, 0, 0), Vec3(0, 1, 0), 
                       60.f, 1.333f, 0.1f, 1000.f);
    
    // Mark all dirty for benchmark
    auto markDirty = [&]() {
        for (auto h : nodes) {
            pipeline.setPosition(h, Vec3(posDist(rng), posDist(rng), posDist(rng)));
        }
    };
    
    BENCHMARK("Transform update (10K nodes)");
    markDirty();
    double us = measureUs([&]() {
        markDirty();
        pipeline.updateTransforms();
    }, 100);
    RESULT(us, std::to_string(int(10000.0 / us * 1000)) + "K nodes/s");
    
    BENCHMARK("Frustum cull (10K nodes)");
    pipeline.updateTransforms();
    us = measureUs([&]() {
        pipeline.frustumCull();
    }, 100);
    RESULT(us, std::to_string(int(10000.0 / us * 1000)) + "K nodes/s");
    
    BENCHMARK("Build draw calls (visible nodes)");
    pipeline.frustumCull();
    us = measureUs([&]() {
        pipeline.buildDrawCalls();
    }, 100);
    RESULT(us, "N/A");
    
    BENCHMARK("Full frame (10K nodes)");
    us = measureUs([&]() {
        markDirty();
        pipeline.renderFrame();
    }, 100);
    RESULT(us, std::to_string(int(10000.0 / us * 1000)) + "K nodes/s");
    
    // Print stats
    const auto& stats = pipeline.stats();
    std::cout << "\n    Pipeline stats:\n"
              << "      Transform update: " << stats.transformUpdateUs.load() << " µs\n"
              << "      Frustum cull:     " << stats.frustumCullUs.load() << " µs\n"
              << "      Draw build:       " << stats.drawBuildUs.load() << " µs\n"
              << "      Visible nodes:    " << stats.visibleNodes.load() << "\n"
              << "      Draw calls:       " << stats.drawCalls.load() << "\n"
              << "      Instances:        " << stats.instancesDrawn.load() << "\n";
    
    // Cleanup
    for (auto h : nodes) {
        pipeline.destroyNode(h);
    }
}


// ═══════════════════════════════════════════════
// Fast Math Approximation Benchmarks
// ═══════════════════════════════════════════════
void benchmarkFastMath() {
    constexpr size_t N = 10000;
    alignas(64) static float inputs[N], outputs[N];
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.1f, 100.f);
    
    for (size_t i = 0; i < N; ++i) {
        inputs[i] = dist(rng);
    }
    
    BENCHMARK("fastInvSqrt (10K)");
    double us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            outputs[i] = fastInvSqrt(inputs[i]);
        }
        escapePointer(outputs);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    BENCHMARK("fastSqrt (10K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            outputs[i] = fastSqrt(inputs[i]);
        }
        escapePointer(outputs);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    std::uniform_real_distribution<float> angleDist(-TWO_PI, TWO_PI);
    for (size_t i = 0; i < N; ++i) {
        inputs[i] = angleDist(rng);
    }

    BENCHMARK("fastSin (10K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            outputs[i] = fastSin(inputs[i]);
        }
        escapePointer(outputs);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");

    BENCHMARK("fastCos (10K)");
    us = measureUs([&]() {
        for (size_t i = 0; i < N; ++i) {
            outputs[i] = fastCos(inputs[i]);
        }
        escapePointer(outputs);
    });
    RESULT(us, std::to_string(us > 0.001 ? int(N / us) : 0) + "M ops/s");
}


// ═══════════════════════════════════════════════
// Accuracy Verification
// ═══════════════════════════════════════════════
void verifyAccuracy() {
    std::cout << "\n\033[1m[ACCURACY]\033[0m Verification\n";
    
    // Test fast math accuracy
    float maxSinErr = 0, maxCosErr = 0, maxSqrtErr = 0;
    
    for (float x = -TWO_PI; x <= TWO_PI; x += 0.001f) {
        float err = std::abs(fastSin(x) - std::sin(x));
        maxSinErr = std::max(maxSinErr, err);
        
        err = std::abs(fastCos(x) - std::cos(x));
        maxCosErr = std::max(maxCosErr, err);
    }
    
    for (float x = 0.01f; x <= 1000.f; x += 0.1f) {
        float err = std::abs(fastSqrt(x) - std::sqrt(x)) / std::sqrt(x);
        maxSqrtErr = std::max(maxSqrtErr, err);
    }
    
    std::cout << "  fastSin max error:  " << std::scientific << maxSinErr << "\n";
    std::cout << "  fastCos max error:  " << std::scientific << maxCosErr << "\n";
    std::cout << "  fastSqrt rel error: " << std::scientific << maxSqrtErr << "\n";
    
    // Verify Vec3 operations
    Vec3 a(1, 2, 3), b(4, 5, 6);
    Vec3 sum = a + b;
    assert(std::abs(sum.x - 5) < 1e-6f);
    assert(std::abs(sum.y - 7) < 1e-6f);
    assert(std::abs(sum.z - 9) < 1e-6f);
    
    float dot = a.dot(b);
    assert(std::abs(dot - 32) < 1e-5f);
    
    Vec3 cross = a.cross(b);
    assert(std::abs(cross.x - (-3)) < 1e-5f);
    assert(std::abs(cross.y - 6) < 1e-5f);
    assert(std::abs(cross.z - (-3)) < 1e-5f);
    
    std::cout << "  Vec3 operations: \033[32mPASS\033[0m\n";
    
    // Verify Mat4 operations
    Mat4 T = Mat4::translate(Vec3(10, 20, 30));
    Vec3 p(1, 2, 3);
    Vec3 tp = T.transformPoint(p);
    assert(std::abs(tp.x - 11) < 1e-5f);
    assert(std::abs(tp.y - 22) < 1e-5f);
    assert(std::abs(tp.z - 33) < 1e-5f);
    
    std::cout << "  Mat4 operations: \033[32mPASS\033[0m\n";
}


// ═══════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════
int main() {
    std::cout << "\n\033[1m╔══════════════════════════════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1m║     MICROSECOND GRAPHICS ENGINE — PERFORMANCE BENCHMARK         ║\033[0m\n";
    std::cout << "\033[1m╚══════════════════════════════════════════════════════════════════╝\033[0m\n\n";
    
    // Detect SIMD support
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
    
    // ── Fast Math ──
    std::cout << "\033[1m[FAST MATH]\033[0m Approximation functions\n";
    benchmarkFastMath();
    
    // ── Vec3/Mat4 ──
    std::cout << "\n\033[1m[SIMD MATH]\033[0m Vector and matrix operations\n";
    benchmarkVec3Operations();
    benchmarkMat4Operations();
    
    // ── Batch Operations ──
    std::cout << "\n\033[1m[BATCH OPS]\033[0m SIMD batch processing\n";
    benchmarkBatchTransform();
    benchmarkBatchMatrixMultiply();
    benchmarkBatchDot();
    
    // ── Memory ──
    std::cout << "\n\033[1m[MEMORY]\033[0m Lock-free allocators\n";
    benchmarkLockFreePool();
    benchmarkThreadLocalArena();
    benchmarkSlabAllocator();
    
    // ── Pipeline ──
    std::cout << "\n\033[1m[PIPELINE]\033[0m Render pipeline operations\n";
    benchmarkTransformUpdate();
    
    // ── Accuracy ──
    verifyAccuracy();
    
    // ── Summary ──
    std::cout << "\n\033[1m╔══════════════════════════════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1m║  All benchmarks completed successfully!                          ║\033[0m\n";
    std::cout << "\033[1m╚══════════════════════════════════════════════════════════════════╝\033[0m\n\n";
    
    return 0;
}