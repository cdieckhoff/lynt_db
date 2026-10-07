/**
 * @file simd_avx2.c
 * @brief AVX2 + FMA implementation of the SIMD API.
 *
 * Compiled with -mavx2 -mfma (GCC/Clang) or /arch:AVX2 (MSVC). Only
 * this file uses AVX2 intrinsics.
 *
 * Each primitive processes 256-bit vectors: 4 u64, 8 u32, 16 u16, or
 * 32 u8 per iteration. No branches in the loop — this is the point of
 * the SIMD selection API.
 *
 * PORTABILITY
 * -----------
 * The scalar tail of the popcount loop uses a portable bit-twiddling
 * helper instead of __builtin_popcount, which MSVC doesn't provide.
 */

#include "simd/simd_internal.h"

#if defined(__x86_64__) || defined(_M_X64)

#include <immintrin.h>
#include <string.h>

 /* ====================================================================
  * Portable bit helpers
  * ==================================================================== */

static inline int simd_popcount8(uint8_t v) {
    v = (uint8_t)(v - ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (int)((v + (v >> 4)) & 0x0F);
}

/* ====================================================================
 * Memory primitives
 * ==================================================================== */

static void avx2_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

static void avx2_memmove(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

static void avx2_memset(void* dst, int value, size_t n) {
    memset(dst, value, n);
}

static int avx2_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}

static void* avx2_memchr(const void* buf, int c, size_t n) {
    return memchr(buf, c, n);
}

/* ====================================================================
 * Search primitives
 * ==================================================================== */

static uint64_t avx2_find_u64(const uint64_t* buf, uint64_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m256i tgt = _mm256_set1_epi64x((int64_t)target);

    for (; i + 4 <= n; i += 4) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(buf + i));
        __m256i eq = _mm256_cmpeq_epi64(v, tgt);
        int bits = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t avx2_find_u32(const uint32_t* buf, uint32_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m256i tgt = _mm256_set1_epi32((int32_t)target);

    for (; i + 8 <= n; i += 8) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(buf + i));
        __m256i eq = _mm256_cmpeq_epi32(v, tgt);
        int bits = _mm256_movemask_ps(_mm256_castsi256_ps(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t avx2_find_u16(const uint16_t* buf, uint16_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m256i tgt = _mm256_set1_epi16((int16_t)target);

    for (; i + 16 <= n; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(buf + i));
        __m256i eq = _mm256_cmpeq_epi16(v, tgt);
        int bits = _mm256_movemask_epi8(eq);
        bits &= 0x55555555;
        uint32_t compact = 0;
        for (int b = 0; b < 16; b++) {
            if (bits & (1 << (b * 2))) compact |= (1u << b);
        }
        mask |= (uint64_t)compact << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t avx2_find_u8(const uint8_t* buf, uint8_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m256i tgt = _mm256_set1_epi8((char)target);

    for (; i + 32 <= n; i += 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(buf + i));
        __m256i eq = _mm256_cmpeq_epi8(v, tgt);
        uint32_t bits = (uint32_t)_mm256_movemask_epi8(eq);
        mask |= (uint64_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Comparison primitives
 * ==================================================================== */

static uint64_t avx2_cmp_u64(const uint64_t* a, const uint64_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 4 <= n; i += 4) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        __m256i eq = _mm256_cmpeq_epi64(va, vb);
        int bits = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t avx2_cmp_u32(const uint32_t* a, const uint32_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 8 <= n; i += 8) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        __m256i eq = _mm256_cmpeq_epi32(va, vb);
        int bits = _mm256_movemask_ps(_mm256_castsi256_ps(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t avx2_cmp_u8(const uint8_t* a, const uint8_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        __m256i eq = _mm256_cmpeq_epi8(va, vb);
        uint32_t bits = (uint32_t)_mm256_movemask_epi8(eq);
        mask |= (uint64_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Bit operations
 * ==================================================================== */

static uint64_t avx2_popcount(const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    uint64_t total = 0;
    size_t i = 0;

    __m256i acc = _mm256_setzero_si256();
    __m256i lookup = _mm256_setr_epi8(
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    __m256i low_mask = _mm256_set1_epi8(0x0F);

    for (; i + 32 <= n; i += 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(p + i));
        __m256i lo = _mm256_and_si256(v, low_mask);
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), low_mask);
        __m256i pcnt = _mm256_add_epi8(
            _mm256_shuffle_epi8(lookup, lo),
            _mm256_shuffle_epi8(lookup, hi));
        acc = _mm256_add_epi8(acc, pcnt);
    }

    __m128i lo128 = _mm256_castsi256_si128(acc);
    __m128i hi128 = _mm256_extracti128_si256(acc, 1);
    __m128i sum128 = _mm_add_epi8(lo128, hi128);
    __m128i sad = _mm_sad_epu8(sum128, _mm_setzero_si128());
    total += (uint64_t)_mm_cvtsi128_si64(sad);
    total += (uint64_t)_mm_extract_epi64(sad, 1);

    for (; i < n; i++) {
        total += (uint64_t)simd_popcount8(p[i]);
    }
    return total;
}

static void avx2_bit_and(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(pa + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(pb + i));
        _mm256_storeu_si256((__m256i*)(d + i), _mm256_and_si256(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] & pb[i];
}

static void avx2_bit_or(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(pa + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(pb + i));
        _mm256_storeu_si256((__m256i*)(d + i), _mm256_or_si256(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] | pb[i];
}

static void avx2_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(pa + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(pb + i));
        _mm256_storeu_si256((__m256i*)(d + i), _mm256_xor_si256(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] ^ pb[i];
}

static void avx2_bit_andnot(void* dst, const void* a, const void* b,
    size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(pa + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(pb + i));
        _mm256_storeu_si256((__m256i*)(d + i), _mm256_andnot_si256(vb, va));
    }
    for (; i < n; i++) d[i] = pa[i] & ~pb[i];
}

/* ====================================================================
 * Hashing — identical to scalar
 * ==================================================================== */

static uint64_t avx2_hash64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    return x;
}

static uint64_t avx2_hash64_buf(const uint64_t* buf, size_t count) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)count;
    for (size_t i = 0; i < count; i++) {
        h = avx2_hash64(h ^ buf[i]);
    }
    return h;
}

/* ====================================================================
 * Backend constructor
 * ==================================================================== */

static const struct simd_backend g_avx2_backend = {
    .isa = SIMD_ISA_AVX2,

    .fn_memcpy = avx2_memcpy,
    .fn_memmove = avx2_memmove,
    .fn_memset = avx2_memset,
    .fn_memcmp = avx2_memcmp,
    .fn_memchr = avx2_memchr,

    .fn_find_u64 = avx2_find_u64,
    .fn_find_u32 = avx2_find_u32,
    .fn_find_u16 = avx2_find_u16,
    .fn_find_u8 = avx2_find_u8,

    .fn_cmp_u64 = avx2_cmp_u64,
    .fn_cmp_u32 = avx2_cmp_u32,
    .fn_cmp_u8 = avx2_cmp_u8,

    .fn_popcount = avx2_popcount,
    .fn_bit_and = avx2_bit_and,
    .fn_bit_or = avx2_bit_or,
    .fn_bit_xor = avx2_bit_xor,
    .fn_bit_andnot = avx2_bit_andnot,

    .fn_hash64 = avx2_hash64,
    .fn_hash64_buf = avx2_hash64_buf,
};

const struct simd_backend* simd_backend_avx2(void) {
    return &g_avx2_backend;
}

#endif  /* x86-64 */