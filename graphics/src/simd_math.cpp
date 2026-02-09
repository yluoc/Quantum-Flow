#include "math/simd_math.h"
#include <cstring>

namespace engine::math::fast {

// ═══════════════════════════════════════════════
// batchTransformPointsFast - Optimized for microsecond performance
// Uses prefetching and processes 4 points at once with SSE
// ═══════════════════════════════════════════════
void batchTransformPointsFast(const Mat4& m, const Vec3* RESTRICT input, 
                               Vec3* RESTRICT output, size_t count)
{
    if (UNLIKELY(count == 0)) return;

#if defined(ENGINE_USE_AVX2)
    // AVX2: Process 2 points simultaneously (each point uses 4 floats in __m256)
    __m256 c0 = _mm256_broadcast_ps(&m.col[0].simd);
    __m256 c1 = _mm256_broadcast_ps(&m.col[1].simd);
    __m256 c2 = _mm256_broadcast_ps(&m.col[2].simd);
    __m256 c3 = _mm256_broadcast_ps(&m.col[3].simd);

    size_t i = 0;
    
    // Prefetch ahead
    constexpr size_t PREFETCH_DISTANCE = 8;
    
    // Process 2 points at a time
    for (; i + 1 < count; i += 2) {
        // Prefetch future data
        if (i + PREFETCH_DISTANCE < count) {
            PREFETCH_READ(&input[i + PREFETCH_DISTANCE]);
            PREFETCH_WRITE(&output[i + PREFETCH_DISTANCE]);
        }

        // Load 2 points (each Vec3 is 16 bytes)
        __m128 p0 = _mm_load_ps((const float*)&input[i]);
        __m128 p1 = _mm_load_ps((const float*)&input[i + 1]);
        
        // Combine into AVX register
        __m256 points = _mm256_set_m128(p1, p0);
        
        // Extract x, y, z components for both points
        __m256 px = _mm256_shuffle_ps(points, points, _MM_SHUFFLE(0,0,0,0));
        __m256 py = _mm256_shuffle_ps(points, points, _MM_SHUFFLE(1,1,1,1));
        __m256 pz = _mm256_shuffle_ps(points, points, _MM_SHUFFLE(2,2,2,2));

        // result = c0*px + c1*py + c2*pz + c3
        __m256 res = _mm256_fmadd_ps(c0, px, c3);
        res = _mm256_fmadd_ps(c1, py, res);
        res = _mm256_fmadd_ps(c2, pz, res);

        // Store results
        _mm_store_ps((float*)&output[i], _mm256_castps256_ps128(res));
        _mm_store_ps((float*)&output[i + 1], _mm256_extractf128_ps(res, 1));
    }

    // Handle remaining point
    if (i < count) {
        __m128 p = _mm_load_ps((const float*)&input[i]);
        __m128 px = _mm_shuffle_ps(p, p, _MM_SHUFFLE(0,0,0,0));
        __m128 py = _mm_shuffle_ps(p, p, _MM_SHUFFLE(1,1,1,1));
        __m128 pz = _mm_shuffle_ps(p, p, _MM_SHUFFLE(2,2,2,2));
        
        __m128 res = _mm_add_ps(m.col[3].simd,
            _mm_add_ps(_mm_mul_ps(m.col[0].simd, px),
                _mm_add_ps(_mm_mul_ps(m.col[1].simd, py),
                    _mm_mul_ps(m.col[2].simd, pz))));
        _mm_store_ps((float*)&output[i], res);
    }

#elif defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_SSE4)
    // SSE2/4: Process 1 point at a time with prefetching
    __m128 c0 = m.col[0].simd;
    __m128 c1 = m.col[1].simd;
    __m128 c2 = m.col[2].simd;
    __m128 c3 = m.col[3].simd;

    constexpr size_t PREFETCH_DISTANCE = 8;

    for (size_t i = 0; i < count; ++i) {
        if (i + PREFETCH_DISTANCE < count) {
            PREFETCH_READ(&input[i + PREFETCH_DISTANCE]);
            PREFETCH_WRITE(&output[i + PREFETCH_DISTANCE]);
        }

        __m128 p = _mm_load_ps((const float*)&input[i]);
        
        __m128 px = _mm_shuffle_ps(p, p, _MM_SHUFFLE(0,0,0,0));
        __m128 py = _mm_shuffle_ps(p, p, _MM_SHUFFLE(1,1,1,1));
        __m128 pz = _mm_shuffle_ps(p, p, _MM_SHUFFLE(2,2,2,2));

        __m128 res = _mm_mul_ps(c0, px);
        res = _mm_add_ps(res, _mm_mul_ps(c1, py));
        res = _mm_add_ps(res, _mm_mul_ps(c2, pz));
        res = _mm_add_ps(res, c3);

        _mm_store_ps((float*)&output[i], res);
    }

#else
    // Scalar fallback with prefetching
    for (size_t i = 0; i < count; ++i) {
        if (i + 8 < count) {
            PREFETCH_READ(&input[i + 8]);
        }
        output[i] = m.transformPoint(input[i]);
    }
#endif
}


// ═══════════════════════════════════════════════
// batchMultiplyMat4Fast - Cache-optimized matrix batch multiply
// ═══════════════════════════════════════════════
void batchMultiplyMat4Fast(const Mat4* RESTRICT parents, const Mat4* RESTRICT locals,
                           Mat4* RESTRICT results, size_t count)
{
    if (UNLIKELY(count == 0)) return;

    constexpr size_t PREFETCH_DISTANCE = 4;

#if defined(ENGINE_USE_AVX2)
    for (size_t i = 0; i < count; ++i) {
        // Prefetch next matrices
        if (i + PREFETCH_DISTANCE < count) {
            PREFETCH_READ(&parents[i + PREFETCH_DISTANCE]);
            PREFETCH_READ(&locals[i + PREFETCH_DISTANCE]);
            PREFETCH_WRITE(&results[i + PREFETCH_DISTANCE]);
        }

        const Mat4& P = parents[i];
        const Mat4& L = locals[i];

        // Load parent columns
        __m128 p0 = P.col[0].simd;
        __m128 p1 = P.col[1].simd;
        __m128 p2 = P.col[2].simd;
        __m128 p3 = P.col[3].simd;

        // Multiply each column of L by P using FMA
        for (int c = 0; c < 4; ++c) {
            __m128 lc = L.col[c].simd;
            __m128 x = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(0,0,0,0));
            __m128 y = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(1,1,1,1));
            __m128 z = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(2,2,2,2));
            __m128 w = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(3,3,3,3));

            // Use FMA for better performance and accuracy
            __m128 res = _mm_fmadd_ps(p0, x, _mm_mul_ps(p3, w));
            res = _mm_fmadd_ps(p1, y, res);
            res = _mm_fmadd_ps(p2, z, res);
            
            _mm_store_ps((float*)&results[i].col[c], res);
        }
    }

#elif defined(ENGINE_USE_SSE2)
    for (size_t i = 0; i < count; ++i) {
        if (i + PREFETCH_DISTANCE < count) {
            PREFETCH_READ(&parents[i + PREFETCH_DISTANCE]);
            PREFETCH_READ(&locals[i + PREFETCH_DISTANCE]);
            PREFETCH_WRITE(&results[i + PREFETCH_DISTANCE]);
        }

        const Mat4& P = parents[i];
        const Mat4& L = locals[i];

        __m128 p0 = P.col[0].simd;
        __m128 p1 = P.col[1].simd;
        __m128 p2 = P.col[2].simd;
        __m128 p3 = P.col[3].simd;

        for (int c = 0; c < 4; ++c) {
            __m128 lc = L.col[c].simd;
            __m128 x = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(0,0,0,0));
            __m128 y = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(1,1,1,1));
            __m128 z = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(2,2,2,2));
            __m128 w = _mm_shuffle_ps(lc, lc, _MM_SHUFFLE(3,3,3,3));

            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(p0, x), _mm_mul_ps(p1, y)),
                _mm_add_ps(_mm_mul_ps(p2, z), _mm_mul_ps(p3, w))
            );
            _mm_store_ps((float*)&results[i].col[c], res);
        }
    }

#else
    for (size_t i = 0; i < count; ++i) {
        results[i] = parents[i] * locals[i];
    }
#endif
}


// ═══════════════════════════════════════════════
// batchDot3Fast - Compute many dot products at once
// AVX2 can process 8 dot products simultaneously
// ═══════════════════════════════════════════════
void batchDot3Fast(const Vec3* RESTRICT a, const Vec3* RESTRICT b, 
                   float* RESTRICT out, size_t count)
{
    if (UNLIKELY(count == 0)) return;

#if defined(ENGINE_USE_AVX2)
    // Process 4 dot products at once using AVX
    size_t i = 0;
    
    for (; i + 3 < count; i += 4) {
        // Prefetch
        if (i + 8 < count) {
            PREFETCH_READ(&a[i + 8]);
            PREFETCH_READ(&b[i + 8]);
        }

        // Load 4 Vec3s from each array
        __m128 a0 = _mm_load_ps((const float*)&a[i]);
        __m128 a1 = _mm_load_ps((const float*)&a[i + 1]);
        __m128 a2 = _mm_load_ps((const float*)&a[i + 2]);
        __m128 a3 = _mm_load_ps((const float*)&a[i + 3]);
        
        __m128 b0 = _mm_load_ps((const float*)&b[i]);
        __m128 b1 = _mm_load_ps((const float*)&b[i + 1]);
        __m128 b2 = _mm_load_ps((const float*)&b[i + 2]);
        __m128 b3 = _mm_load_ps((const float*)&b[i + 3]);

        // Compute dot products (mask 0x71 = xyz only, result in lowest float)
        __m128 d0 = _mm_dp_ps(a0, b0, 0x71);
        __m128 d1 = _mm_dp_ps(a1, b1, 0x71);
        __m128 d2 = _mm_dp_ps(a2, b2, 0x71);
        __m128 d3 = _mm_dp_ps(a3, b3, 0x71);

        // Combine results
        __m128 d01 = _mm_unpacklo_ps(d0, d1);
        __m128 d23 = _mm_unpacklo_ps(d2, d3);
        __m128 result = _mm_movelh_ps(d01, d23);
        
        _mm_storeu_ps(&out[i], result);
    }

    // Handle remaining
    for (; i < count; ++i) {
        out[i] = a[i].dot(b[i]);
    }

#elif defined(ENGINE_USE_SSE4)
    for (size_t i = 0; i < count; ++i) {
        __m128 va = _mm_load_ps((const float*)&a[i]);
        __m128 vb = _mm_load_ps((const float*)&b[i]);
        out[i] = _mm_cvtss_f32(_mm_dp_ps(va, vb, 0x71));
    }

#elif defined(ENGINE_USE_SSE2)
    for (size_t i = 0; i < count; ++i) {
        __m128 va = _mm_load_ps((const float*)&a[i]);
        __m128 vb = _mm_load_ps((const float*)&b[i]);
        __m128 mul = _mm_mul_ps(va, vb);
        __m128 s1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,0,3,2));
        __m128 sum = _mm_add_ps(mul, s1);
        __m128 s2 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0,1,0,1));
        out[i] = _mm_cvtss_f32(_mm_add_ps(sum, s2));
    }

#else
    for (size_t i = 0; i < count; ++i) {
        out[i] = a[i].dot(b[i]);
    }
#endif
}


// ═══════════════════════════════════════════════
// batchNormalize3 - Normalize vectors in bulk
// ═══════════════════════════════════════════════
void batchNormalize3(Vec3* RESTRICT vectors, size_t count)
{
    if (UNLIKELY(count == 0)) return;

#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2)
    for (size_t i = 0; i < count; ++i) {
        if (i + 8 < count) {
            PREFETCH_READ(&vectors[i + 8]);
        }

        __m128 v = _mm_load_ps((const float*)&vectors[i]);
        __m128 dp = _mm_dp_ps(v, v, 0x7F);  // dot product xyz
        __m128 inv = _mm_rsqrt_ps(dp);      // 1/sqrt approximation
        
        // Newton-Raphson refinement for better precision
        __m128 half = _mm_set1_ps(0.5f);
        __m128 three = _mm_set1_ps(3.0f);
        inv = _mm_mul_ps(_mm_mul_ps(half, inv), 
                         _mm_sub_ps(three, _mm_mul_ps(dp, _mm_mul_ps(inv, inv))));
        
        __m128 result = _mm_mul_ps(v, inv);
        _mm_store_ps((float*)&vectors[i], result);
    }

#elif defined(ENGINE_USE_SSE2)
    for (size_t i = 0; i < count; ++i) {
        __m128 v = _mm_load_ps((const float*)&vectors[i]);
        __m128 mul = _mm_mul_ps(v, v);
        
        // Horizontal add for dot product
        __m128 s1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,0,3,2));
        __m128 sum = _mm_add_ps(mul, s1);
        __m128 s2 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0,1,0,1));
        __m128 dp = _mm_add_ps(sum, s2);
        
        __m128 inv = _mm_rsqrt_ps(dp);
        __m128 result = _mm_mul_ps(v, inv);
        _mm_store_ps((float*)&vectors[i], result);
    }

#else
    for (size_t i = 0; i < count; ++i) {
        vectors[i].normalize();
    }
#endif
}


// ═══════════════════════════════════════════════
// batchAABBTest - Fast frustum culling
// Tests N AABBs against frustum planes simultaneously
// ═══════════════════════════════════════════════
void batchAABBTest(const Vec3* RESTRICT mins, const Vec3* RESTRICT maxs,
                   const Vec4* RESTRICT planes, uint8_t* RESTRICT results, 
                   size_t count, size_t numPlanes)
{
    if (UNLIKELY(count == 0 || numPlanes == 0)) return;

#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2)
    // Pre-load plane normals and distances
    __m128 planeNormals[6];
    __m128 planeDists[6];
    
    for (size_t p = 0; p < numPlanes && p < 6; ++p) {
        planeNormals[p] = _mm_load_ps((const float*)&planes[p]);
        planeDists[p] = _mm_shuffle_ps(planeNormals[p], planeNormals[p], _MM_SHUFFLE(3,3,3,3));
    }

    for (size_t i = 0; i < count; ++i) {
        if (i + 4 < count) {
            PREFETCH_READ(&mins[i + 4]);
            PREFETCH_READ(&maxs[i + 4]);
        }

        __m128 minV = _mm_load_ps((const float*)&mins[i]);
        __m128 maxV = _mm_load_ps((const float*)&maxs[i]);
        
        bool inside = true;
        
        for (size_t p = 0; p < numPlanes && p < 6 && inside; ++p) {
            // Select positive vertex (furthest in direction of normal)
            __m128 normal = planeNormals[p];
            __m128 zero = _mm_setzero_ps();
            __m128 mask = _mm_cmpgt_ps(normal, zero);
            
            // positiveVertex = (normal > 0) ? max : min
            __m128 posVertex = _mm_or_ps(
                _mm_and_ps(mask, maxV),
                _mm_andnot_ps(mask, minV)
            );
            
            // Compute dot product of positive vertex with plane normal
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2)
            __m128 dp = _mm_dp_ps(posVertex, normal, 0x71);
#else
            __m128 mul = _mm_mul_ps(posVertex, normal);
            __m128 s1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,0,3,2));
            __m128 sum = _mm_add_ps(mul, s1);
            __m128 s2 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0,1,0,1));
            __m128 dp = _mm_add_ps(sum, s2);
#endif
            // Add plane distance
            __m128 dist = _mm_add_ps(dp, planeDists[p]);
            
            // If dist < 0, AABB is outside this plane
            if (_mm_cvtss_f32(dist) < 0.0f) {
                inside = false;
            }
        }
        
        results[i] = inside ? 1 : 0;
    }

#else
    // Scalar fallback
    for (size_t i = 0; i < count; ++i) {
        bool inside = true;
        
        for (size_t p = 0; p < numPlanes && inside; ++p) {
            Vec3 normal(planes[p].x, planes[p].y, planes[p].z);
            float dist = planes[p].w;
            
            // Find positive vertex
            Vec3 posVertex(
                (normal.x > 0) ? maxs[i].x : mins[i].x,
                (normal.y > 0) ? maxs[i].y : mins[i].y,
                (normal.z > 0) ? maxs[i].z : mins[i].z
            );
            
            if (posVertex.dot(normal) + dist < 0.0f) {
                inside = false;
            }
        }
        
        results[i] = inside ? 1 : 0;
    }
#endif
}

} // namespace engine::math::fast