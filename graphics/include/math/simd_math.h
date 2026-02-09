#pragma once

#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <array>
#include <numeric>

// ─────────────────────────────────────────────
// Aggressive SIMD feature detection
// ─────────────────────────────────────────────
#if defined(__AVX512F__)
  #define ENGINE_USE_AVX512 1
  #define ENGINE_USE_AVX2 1
  #define ENGINE_USE_SSE4 1
  #define ENGINE_USE_SSE2 1
  #include <immintrin.h>
#elif defined(__AVX2__)
  #define ENGINE_USE_AVX2 1
  #define ENGINE_USE_SSE4 1
  #define ENGINE_USE_SSE2 1
  #include <immintrin.h>
#elif defined(__SSE4_1__)
  #define ENGINE_USE_SSE4 1
  #define ENGINE_USE_SSE2 1
  #include <smmintrin.h>
#elif defined(__SSE2__)
  #define ENGINE_USE_SSE2 1
  #include <emmintrin.h>
#else
  #define ENGINE_USE_SCALAR 1
#endif

// Prefetch hints for cache optimization
#if defined(__GNUC__) || defined(__clang__)
  #define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
  #define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
  #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define FORCE_INLINE __attribute__((always_inline)) inline
  #define RESTRICT __restrict__
#else
  #define PREFETCH_READ(addr)
  #define PREFETCH_WRITE(addr)
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
  #define FORCE_INLINE inline
  #define RESTRICT
#endif

namespace engine {
namespace math {
namespace fast {

// ═══════════════════════════════════════════════
// Constants - compile-time optimized
// ═══════════════════════════════════════════════
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float INV_PI = 0.31830988618379067154f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

// ═══════════════════════════════════════════════
// Fast math approximations (microsecond-level)
// ═══════════════════════════════════════════════

// Fast inverse square root
FORCE_INLINE float fastInvSqrt(float x) {
    float xhalf = 0.5f * x;
    int32_t i;
    std::memcpy(&i, &x, sizeof(i));
    i = 0x5f375a86 - (i >> 1);  // Magic Number
    std::memcpy(&x, &i, sizeof(x));
    x = x * (1.5f - xhalf * x * x);  // Newton iteration
    x = x * (1.5f - xhalf * x * x);  // Second iteration for precision
    return x;
}

FORCE_INLINE float fastSqrt(float x) {
    if (UNLIKELY(x <= 0.f)) return 0.f; // zero guard
    return x * fastInvSqrt(x);
}

// Fast sin/cos using polynomial approximation
FORCE_INLINE float fastSin(float x) {
    // Normalize to [-PI, PI]
    x = x - TWO_PI * std::floor((x + PI) * INV_PI * 0.5f);

    // Improve accuracy by folding to [-PI/2, PI/2]
    if (x > HALF_PI) {
        x = PI - x;
    } else if (x < -HALF_PI) {
        x = -PI - x;
    }

    // tight near PI/2
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f
        - x2 * (0.0083333337f
        - x2 * (0.00019841270f
        - x2 * 0.0000027557319f))));
}

FORCE_INLINE float fastCos(float x) {
    return fastSin(x + HALF_PI);
}

// Fast atan2 approximation
FORCE_INLINE float fastAtan2(float y, float x) {
    float ax = std::abs(x), ay = std::abs(y);
    float mn = std::min(ax, ay), mx = std::max(ax, ay);
    float a = mn / (mx + 1e-10f);
    
    // Polynomial approximation
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    
    if (ay > ax) r = HALF_PI - r;
    if (x < 0) r = PI - r;
    if (y < 0) r = -r;
    return r;
}

// ═══════════════════════════════════════════════
// Vec2 - SIMD optimized
// ═══════════════════════════════════════════════
struct alignas(16) Vec2 {
    union {
        struct { float x, y, _pad0, _pad1; };
        float data[4];
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        __m128 simd;
#endif
    };

    Vec2() : x(0.f), y(0.f), _pad0(0.f), _pad1(0.f) {}
    Vec2(float x_, float y_) : x(x_), y(y_), _pad0(0.f), _pad1(0.f) {}
    explicit Vec2(float v) : x(v), y(v), _pad0(0.f), _pad1(0.f) {}

#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
    Vec2(__m128 v) : simd(v) {}
    operator __m128() const { return simd; }
#endif

    FORCE_INLINE Vec2 operator+(const Vec2& r) const {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return Vec2(_mm_add_ps(simd, r.simd));
#else
        return {x+r.x, y+r.y};
#endif
    }

    FORCE_INLINE Vec2 operator-(const Vec2& r) const {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return Vec2(_mm_sub_ps(simd, r.simd));
#else
        return {x-r.x, y-r.y};
#endif
    }

    FORCE_INLINE Vec2 operator*(float s) const {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return Vec2(_mm_mul_ps(simd, _mm_set1_ps(s)));
#else
        return {x*s, y*s};
#endif
    }

    FORCE_INLINE Vec2 operator/(float s) const {
        float inv = 1.f/s;
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return Vec2(_mm_mul_ps(simd, _mm_set1_ps(inv)));
#else
        return {x*inv, y*inv};
#endif
    }

    FORCE_INLINE Vec2& operator+=(const Vec2& r) {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        simd = _mm_add_ps(simd, r.simd);
#else
        x+=r.x; y+=r.y;
#endif
        return *this;
    }

    FORCE_INLINE Vec2& operator-=(const Vec2& r) {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        simd = _mm_sub_ps(simd, r.simd);
#else
        x-=r.x; y-=r.y;
#endif
        return *this;
    }

    FORCE_INLINE Vec2& operator*=(float s) {
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        simd = _mm_mul_ps(simd, _mm_set1_ps(s));
#else
        x*=s; y*=s;
#endif
        return *this;
    }

    FORCE_INLINE float dot(const Vec2& r) const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return _mm_cvtss_f32(_mm_dp_ps(simd, r.simd, 0x31));
#elif defined(ENGINE_USE_SSE2)
        __m128 mul = _mm_mul_ps(simd, r.simd);
        __m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,1,1,1));
        __m128 sum = _mm_add_ss(mul, shuf);
        return _mm_cvtss_f32(sum);
#else
        return x*r.x + y*r.y;
#endif
    }

    FORCE_INLINE float lengthSq() const { return dot(*this); }

    FORCE_INLINE float length() const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        __m128 dp = _mm_dp_ps(simd, simd, 0x3F);
        return _mm_cvtss_f32(_mm_sqrt_ss(dp));
#else
        return fastSqrt(lengthSq());
#endif
    }

    FORCE_INLINE Vec2 normalized() const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        __m128 dp = _mm_dp_ps(simd, simd, 0x3F);
        float l2 = _mm_cvtss_f32(dp);
        if (l2 > 0.f) {
            __m128 inv = _mm_rsqrt_ps(dp);
            // One Newton-Raphson step for better accuracy
            __m128 y2 = _mm_mul_ps(inv, inv);
            __m128 half_x_y2 = _mm_mul_ps(_mm_mul_ps(dp, y2), _mm_set1_ps(0.5f));
            __m128 term = _mm_sub_ps(_mm_set1_ps(1.5f), half_x_y2);
            inv = _mm_mul_ps(inv, term);
            return Vec2(_mm_mul_ps(simd, inv));
        }
        return Vec2();
#else
        float l2 = lengthSq();
        return l2 > 0.f ? *this * fastInvSqrt(l2) : Vec2();
#endif
    }

    FORCE_INLINE void normalize() { *this = normalized(); }

    static Vec2 zero() { return Vec2(0.f, 0.f); }
    static Vec2 one() { return Vec2(1.f, 1.f); }
};



// ═══════════════════════════════════════════════
// Vec3 - SIMD optimized
// ═══════════════════════════════════════════════
struct alignas(16) Vec3 {
    union {
        struct { float x, y, z, _pad; };
        float data[4];
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        __m128 simd;
#endif
    };

    Vec3() : x(0.f), y(0.f), z(0.f), _pad(0.f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_), _pad(0.f) {}
    explicit Vec3(float v) : x(v), y(v), z(v), _pad(0.f) {}
    
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
    Vec3(__m128 v) : simd(v) {}
    operator __m128() const { return simd; }
#endif

    FORCE_INLINE Vec3 operator+(const Vec3& r) const {
#if defined(ENGINE_USE_SSE2)
        return Vec3(_mm_add_ps(simd, r.simd));
#else
        return {x+r.x, y+r.y, z+r.z};
#endif
    }

    FORCE_INLINE Vec3 operator-(const Vec3& r) const {
#if defined(ENGINE_USE_SSE2)
        return Vec3(_mm_sub_ps(simd, r.simd));
#else
        return {x-r.x, y-r.y, z-r.z};
#endif
    }

    FORCE_INLINE Vec3 operator*(float s) const {
#if defined(ENGINE_USE_SSE2)
        return Vec3(_mm_mul_ps(simd, _mm_set1_ps(s)));
#else
        return {x*s, y*s, z*s};
#endif
    }

    FORCE_INLINE Vec3 operator*(const Vec3& r) const {
#if defined(ENGINE_USE_SSE2)
        return Vec3(_mm_mul_ps(simd, r.simd));
#else
        return {x*r.x, y*r.y, z*r.z};
#endif
    }

    FORCE_INLINE float dot(const Vec3& r) const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        return _mm_cvtss_f32(_mm_dp_ps(simd, r.simd, 0x71));
#elif defined(ENGINE_USE_SSE2)
        __m128 mul = _mm_mul_ps(simd, r.simd);
        __m128 s1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,0,3,2));
        __m128 sum = _mm_add_ps(mul, s1);
        __m128 s2 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0,1,0,1));
        return _mm_cvtss_f32(_mm_add_ps(sum, s2));
#else
        return x*r.x + y*r.y + z*r.z;
#endif
    }

    FORCE_INLINE Vec3 cross(const Vec3& r) const {
#if defined(ENGINE_USE_SSE2)
        __m128 a_yzx = _mm_shuffle_ps(simd, simd, _MM_SHUFFLE(3,0,2,1));
        __m128 b_yzx = _mm_shuffle_ps(r.simd, r.simd, _MM_SHUFFLE(3,0,2,1));
        __m128 c = _mm_sub_ps(_mm_mul_ps(simd, b_yzx), _mm_mul_ps(a_yzx, r.simd));
        return Vec3(_mm_shuffle_ps(c, c, _MM_SHUFFLE(3,0,2,1)));
#else
        return { y*r.z - z*r.y, z*r.x - x*r.z, x*r.y - y*r.x };
#endif
    }

    FORCE_INLINE float lengthSq() const { return dot(*this); }
    
    FORCE_INLINE float length() const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2)
        __m128 dp = _mm_dp_ps(simd, simd, 0x7F);
        return _mm_cvtss_f32(_mm_sqrt_ss(dp));
#else
        return fastSqrt(lengthSq());
#endif
    }

    FORCE_INLINE Vec3 normalized() const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2)
        __m128 dp = _mm_dp_ps(simd, simd, 0x7F);
        float l2 = _mm_cvtss_f32(dp);
        if (l2 > 0.f) {
            __m128 inv = _mm_rsqrt_ps(dp);
            // Newton-Raphson refinement for better accuracy
            __m128 y2 = _mm_mul_ps(inv, inv);
            __m128 half_x_y2 = _mm_mul_ps(_mm_mul_ps(dp, y2), _mm_set1_ps(0.5f));
            __m128 term = _mm_sub_ps(_mm_set1_ps(1.5f), half_x_y2);
            inv = _mm_mul_ps(inv, term);
            return Vec3(_mm_mul_ps(simd, inv));
        }
        return Vec3();
#else
        float l2 = lengthSq();
        return l2 > 0.f ? *this * fastInvSqrt(l2) : Vec3();
#endif
    }

    FORCE_INLINE void normalize() {
        *this = normalized();
    }

    static Vec3 zero() { return {0.f, 0.f, 0.f}; }
    static Vec3 one() { return {1.f, 1.f, 1.f}; }
    static Vec3 up() { return {0.f, 1.f, 0.f}; }
};


// ═══════════════════════════════════════════════
// Vec4 - SIMD optimized
// ═══════════════════════════════════════════════
struct alignas(16) Vec4 {
    union {
        struct { float x, y, z, w; };
        float data[4];
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
        __m128 simd;
#endif
    };

    Vec4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2) || defined(ENGINE_USE_AVX512)
    Vec4(__m128 v) : simd(v) {}
    operator __m128() const { return simd; }
#endif

    FORCE_INLINE Vec4 operator+(const Vec4& r) const {
#if defined(ENGINE_USE_SSE2)
        return Vec4(_mm_add_ps(simd, r.simd));
#else
        return {x+r.x, y+r.y, z+r.z, w+r.w};
#endif
    }

    FORCE_INLINE Vec4 operator*(float s) const {
#if defined(ENGINE_USE_SSE2)
        return Vec4(_mm_mul_ps(simd, _mm_set1_ps(s)));
#else
        return {x*s, y*s, z*s, w*s};
#endif
    }

    FORCE_INLINE float dot(const Vec4& r) const {
#if defined(ENGINE_USE_SSE4) || defined(ENGINE_USE_AVX2)
        return _mm_cvtss_f32(_mm_dp_ps(simd, r.simd, 0xF1));
#else
        return x*r.x + y*r.y + z*r.z + w*r.w;
#endif
    }

    Vec3 xyz() const { return {x, y, z}; }
    const float* ptr() const { return &x; }
    float* ptr() { return &x; }
};


// ═══════════════════════════════════════════════
// Mat4 - Column-major, 64-byte aligned for cache
// ═══════════════════════════════════════════════
struct alignas(64) Mat4 {
    Vec4 col[4];

    Mat4() { col[0]=col[1]=col[2]=col[3]=Vec4(); }

    static Mat4 identity() {
        Mat4 m;
        m.col[0] = {1,0,0,0};
        m.col[1] = {0,1,0,0};
        m.col[2] = {0,0,1,0};
        m.col[3] = {0,0,0,1};
        return m;
    }

    float& operator()(int c, int r) { return col[c].data[r]; }
    float operator()(int c, int r) const { return col[c].data[r]; }

    const float* data() const { return (const float*)col; }
    float* data() { return (float*)col; }

    // SIMD-optimized matrix multiplication
    FORCE_INLINE Mat4 operator*(const Mat4& r) const {
        Mat4 res;
#if defined(ENGINE_USE_SSE2) || defined(ENGINE_USE_AVX2)
        __m128 c0 = col[0].simd;
        __m128 c1 = col[1].simd;
        __m128 c2 = col[2].simd;
        __m128 c3 = col[3].simd;

        for (int i = 0; i < 4; ++i) {
            __m128 rc = r.col[i].simd;
            __m128 x = _mm_shuffle_ps(rc, rc, _MM_SHUFFLE(0,0,0,0));
            __m128 y = _mm_shuffle_ps(rc, rc, _MM_SHUFFLE(1,1,1,1));
            __m128 z = _mm_shuffle_ps(rc, rc, _MM_SHUFFLE(2,2,2,2));
            __m128 w = _mm_shuffle_ps(rc, rc, _MM_SHUFFLE(3,3,3,3));
            
            res.col[i].simd = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(c0, x), _mm_mul_ps(c1, y)),
                _mm_add_ps(_mm_mul_ps(c2, z), _mm_mul_ps(c3, w))
            );
        }
#else
        for (int c = 0; c < 4; ++c) {
            res.col[c].x = col[0].x*r.col[c].x + col[1].x*r.col[c].y + col[2].x*r.col[c].z + col[3].x*r.col[c].w;
            res.col[c].y = col[0].y*r.col[c].x + col[1].y*r.col[c].y + col[2].y*r.col[c].z + col[3].y*r.col[c].w;
            res.col[c].z = col[0].z*r.col[c].x + col[1].z*r.col[c].y + col[2].z*r.col[c].z + col[3].z*r.col[c].w;
            res.col[c].w = col[0].w*r.col[c].x + col[1].w*r.col[c].y + col[2].w*r.col[c].z + col[3].w*r.col[c].w;
        }
#endif
        return res;
    }

    FORCE_INLINE Vec3 transformPoint(const Vec3& v) const {
#if defined(ENGINE_USE_SSE2)
        __m128 px = _mm_set1_ps(v.x);
        __m128 py = _mm_set1_ps(v.y);
        __m128 pz = _mm_set1_ps(v.z);
        
        __m128 res = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(col[0].simd, px), _mm_mul_ps(col[1].simd, py)),
            _mm_add_ps(_mm_mul_ps(col[2].simd, pz), col[3].simd)
        );
        return Vec3(res);
#else
        return {
            col[0].x*v.x + col[1].x*v.y + col[2].x*v.z + col[3].x,
            col[0].y*v.x + col[1].y*v.y + col[2].y*v.z + col[3].y,
            col[0].z*v.x + col[1].z*v.y + col[2].z*v.z + col[3].z
        };
#endif
    }

    FORCE_INLINE Vec3 transformDir(const Vec3& v) const {
#if defined(ENGINE_USE_SSE2)
        __m128 px = _mm_set1_ps(v.x);
        __m128 py = _mm_set1_ps(v.y);
        __m128 pz = _mm_set1_ps(v.z);
        
        __m128 res = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(col[0].simd, px), _mm_mul_ps(col[1].simd, py)),
            _mm_mul_ps(col[2].simd, pz)
        );
        return Vec3(res);
#else
        return {
            col[0].x*v.x + col[1].x*v.y + col[2].x*v.z,
            col[0].y*v.x + col[1].y*v.y + col[2].y*v.z,
            col[0].z*v.x + col[1].z*v.y + col[2].z*v.z
        };
#endif
    }

    static Mat4 translate(const Vec3& t) {
        Mat4 m = identity();
        m.col[3] = {t.x, t.y, t.z, 1.f};
        return m;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 m = identity();
        m.col[0].x = s.x;
        m.col[1].y = s.y;
        m.col[2].z = s.z;
        return m;
    }

    static Mat4 rotateY(float rad) {
        float c = fastCos(rad), s = fastSin(rad);
        Mat4 m = identity();
        m.col[0].x = c; m.col[0].z = -s;
        m.col[2].x = s; m.col[2].z = c;
        return m;
    }

    static Mat4 rotateX(float rad) {
        float c = fastCos(rad), s = fastSin(rad);
        Mat4 m = identity();
        m.col[1].y = c; m.col[1].z = s;
        m.col[2].y = -s; m.col[2].z = c;
        return m;
    }

    static Mat4 rotateZ(float rad) {
        float c = fastCos(rad), s = fastSin(rad);
        Mat4 m = identity();
        m.col[0].x = c; m.col[0].y = s;
        m.col[1].x = -s; m.col[1].y = c;
        return m;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 m = identity();
        m.col[0] = { s.x, u.x, -f.x, 0.f};
        m.col[1] = { s.y, u.y, -f.y, 0.f};
        m.col[2] = { s.z, u.z, -f.z, 0.f};
        m.col[3] = {-s.dot(eye), -u.dot(eye), f.dot(eye), 1.f};
        return m;
    }

    static Mat4 perspective(float fovRad, float aspect, float nearP, float farP) {
        float f = 1.f / std::tan(fovRad * 0.5f);
        Mat4 m;
        m.col[0].x = f / aspect;
        m.col[1].y = f;
        m.col[2].z = (farP + nearP) / (nearP - farP);
        m.col[2].w = -1.f;
        m.col[3].z = (2.f * farP * nearP) / (nearP - farP);
        return m;
    }

    Mat4 transpose() const {
        Mat4 m;
#if defined(ENGINE_USE_SSE2)
        __m128 t0 = _mm_unpacklo_ps(col[0].simd, col[1].simd);
        __m128 t1 = _mm_unpackhi_ps(col[0].simd, col[1].simd);
        __m128 t2 = _mm_unpacklo_ps(col[2].simd, col[3].simd);
        __m128 t3 = _mm_unpackhi_ps(col[2].simd, col[3].simd);
        m.col[0].simd = _mm_movelh_ps(t0, t2);
        m.col[1].simd = _mm_movehl_ps(t2, t0);
        m.col[2].simd = _mm_movelh_ps(t1, t3);
        m.col[3].simd = _mm_movehl_ps(t3, t1);
#else
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                m(c,r) = (*this)(r,c);
#endif
        return m;
    }

    Mat4 inverse3x3() const {
        float m00=col[0].x, m01=col[1].x, m02=col[2].x;
        float m10=col[0].y, m11=col[1].y, m12=col[2].y;
        float m20=col[0].z, m21=col[1].z, m22=col[2].z;

        float det = m00*(m11*m22-m12*m21) - m01*(m10*m22-m12*m20) + m02*(m10*m21-m11*m20);
        float inv = (std::fabs(det) > 1e-10f) ? 1.f/det : 0.f;

        Mat4 r = identity();
        r.col[0].x = (m11*m22-m12*m21)*inv;
        r.col[0].y = (m12*m20-m10*m22)*inv;
        r.col[0].z = (m10*m21-m11*m20)*inv;
        r.col[1].x = (m02*m21-m01*m22)*inv;
        r.col[1].y = (m00*m22-m02*m20)*inv;
        r.col[1].z = (m01*m20-m00*m21)*inv;
        r.col[2].x = (m01*m12-m02*m11)*inv;
        r.col[2].y = (m02*m10-m00*m12)*inv;
        r.col[2].z = (m00*m11-m01*m10)*inv;
        return r;
    }
};


// ═══════════════════════════════════════════════
// Ultra-fast batch operations with AVX2/AVX-512
// ═══════════════════════════════════════════════

// Transform N points - processes 4/8 at once
void batchTransformPointsFast(const Mat4& m, const Vec3* RESTRICT input, 
                               Vec3* RESTRICT output, size_t count);

// Batch matrix multiply with prefetching
void batchMultiplyMat4Fast(const Mat4* RESTRICT parents, const Mat4* RESTRICT locals,
                           Mat4* RESTRICT results, size_t count);

// Batch dot product - 8 at a time with AVX
void batchDot3Fast(const Vec3* RESTRICT a, const Vec3* RESTRICT b, 
                   float* RESTRICT out, size_t count);

// Batch normalize vectors
void batchNormalize3(Vec3* RESTRICT vectors, size_t count);

// Batch AABB test (for frustum culling)
void batchAABBTest(const Vec3* RESTRICT mins, const Vec3* RESTRICT maxs,
                   const Vec4* RESTRICT planes, uint8_t* RESTRICT results, 
                   size_t count, size_t numPlanes);


// ═══════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════
FORCE_INLINE float clamp(float v, float lo, float hi) { 
    return v < lo ? lo : (v > hi ? hi : v); 
}
FORCE_INLINE float lerp(float a, float b, float t) { return a + (b - a) * t; }
FORCE_INLINE float radians(float deg) { return deg * DEG_TO_RAD; }
FORCE_INLINE float degrees(float rad) { return rad * RAD_TO_DEG; }

} // namespace fast
} // namespace math
} // namespace engine
