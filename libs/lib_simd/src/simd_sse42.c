/**
 * @file simd_sse42.c
 * @brief SSE4.2 implementation of the SIMD API.
 *
 * Compiled with -msse4.2 (GCC/Clang) on x86-64. Fallback for CPUs
 * without AVX2. Each primitive processes 128-bit vectors: 2 u64,
 * 4 u32, 8 u16, or 16 u8 per iteration.
 *
 * PORTABILITY NOTES
 * -----------------
 * Only included and compiled on x86-64 (guarded by __x86_64__ or
 * _M_X64). The SSE4.2 ISA flags are set per-file by CMake; if they're
 * missing, this file will still compile but the compiler may fall back
 * to scalar code for some intrinsics, which is not a correctness
 * issue.
 *
 * No __builtin_popcount. MSVC doesn't have it, so the scalar tail
 * uses a bit-twiddling helper.
 */

#include "simd/simd_internal.h"

#if defined(__x86_64__) || defined(_M_X64)

#include <immintrin.h>
#include <string.h>

 /* ====================================================================
  * Portable bit helpers
  * ==================================================================== */

  /**
   * @brief Count the number of set bits in a single byte.
   *
   * Bit-twiddling popcount. Portable to every compiler, used in the
   * scalar tail of the popcount loop.
   */
static inline int simd_popcount8(uint8_t v) {
    v = (uint8_t)(v - ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (int)((v + (v >> 4)) & 0x0F);
}

/* ====================================================================
 * Memory primitives
 * ==================================================================== */

static void sse42_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

static void sse42_memmove(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

static void sse42_memset(void* dst, int value, size_t n) {
    memset(dst, value, n);
}

static int sse42_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}

static void* sse42_memchr(const void* buf, int c, size_t n) {
    return memchr(buf, c, n);
}

/* ====================================================================
 * Search primitives
 * ==================================================================== */

static uint64_t sse42_find_u64(const uint64_t* buf, uint64_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m128i tgt = _mm_set1_epi64x((int64_t)target);

    for (; i + 2 <= n; i += 2) {
        __m128i v = _mm_loadu_si128((const __m128i*)(buf + i));
        __m128i eq = _mm_cmpeq_epi64(v, tgt);
        int bits = _mm_movemask_pd(_mm_castsi128_pd(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t sse42_find_u32(const uint32_t* buf, uint32_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m128i tgt = _mm_set1_epi32((int32_t)target);

    for (; i + 4 <= n; i += 4) {
        __m128i v = _mm_loadu_si128((const __m128i*)(buf + i));
        __m128i eq = _mm_cmpeq_epi32(v, tgt);
        int bits = _mm_movemask_ps(_mm_castsi128_ps(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t sse42_find_u16(const uint16_t* buf, uint16_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m128i tgt = _mm_set1_epi16((int16_t)target);

    for (; i + 8 <= n; i += 8) {
        __m128i v = _mm_loadu_si128((const __m128i*)(buf + i));
        __m128i eq = _mm_cmpeq_epi16(v, tgt);
        int bits = _mm_movemask_epi8(eq);
        bits &= 0x5555;
        uint32_t compact = 0;
        for (int b = 0; b < 8; b++) {
            if (bits & (1 << (b * 2))) compact |= (1u << b);
        }
        mask |= (uint64_t)compact << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t sse42_find_u8(const uint8_t* buf, uint8_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    __m128i tgt = _mm_set1_epi8((char)target);

    for (; i + 16 <= n; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(buf + i));
        __m128i eq = _mm_cmpeq_epi8(v, tgt);
        int bits = _mm_movemask_epi8(eq);
        mask |= (uint64_t)(uint16_t)bits << i;
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Comparison primitives
 * ==================================================================== */

static uint64_t sse42_cmp_u64(const uint64_t* a, const uint64_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 2 <= n; i += 2) {
        __m128i va = _mm_loadu_si128((const __m128i*)(a + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(b + i));
        __m128i eq = _mm_cmpeq_epi64(va, vb);
        int bits = _mm_movemask_pd(_mm_castsi128_pd(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t sse42_cmp_u32(const uint32_t* a, const uint32_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 4 <= n; i += 4) {
        __m128i va = _mm_loadu_si128((const __m128i*)(a + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(b + i));
        __m128i eq = _mm_cmpeq_epi32(va, vb);
        int bits = _mm_movemask_ps(_mm_castsi128_ps(eq));
        mask |= (uint64_t)(uint8_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t sse42_cmp_u8(const uint8_t* a, const uint8_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(a + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(b + i));
        __m128i eq = _mm_cmpeq_epi8(va, vb);
        int bits = _mm_movemask_epi8(eq);
        mask |= (uint64_t)(uint16_t)bits << i;
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Bit operations
 * ==================================================================== */

static uint64_t sse42_popcount(const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    uint64_t total = 0;
    size_t i = 0;

    __m128i acc = _mm_setzero_si128();
    __m128i lookup = _mm_setr_epi8(
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    __m128i low_mask = _mm_set1_epi8(0x0F);

    for (; i + 16 <= n; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(p + i));
        __m128i lo = _mm_and_si128(v, low_mask);
        __m128i hi = _mm_and_si128(_mm_srli_epi16(v, 4), low_mask);
        __m128i pcnt = _mm_add_epi8(
            _mm_shuffle_epi8(lookup, lo),
            _mm_shuffle_epi8(lookup, hi));
        acc = _mm_add_epi8(acc, pcnt);
    }

    __m128i sad = _mm_sad_epu8(acc, _mm_setzero_si128());
    total += (uint64_t)_mm_cvtsi128_si64(sad);
    total += (uint64_t)_mm_extract_epi64(sad, 1);

    for (; i < n; i++) {
        total += (uint64_t)simd_popcount8(p[i]);
    }
    return total;
}

static void sse42_bit_and(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(pa + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(pb + i));
        _mm_storeu_si128((__m128i*)(d + i), _mm_and_si128(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] & pb[i];
}

static void sse42_bit_or(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(pa + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(pb + i));
        _mm_storeu_si128((__m128i*)(d + i), _mm_or_si128(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] | pb[i];
}

static void sse42_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(pa + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(pb + i));
        _mm_storeu_si128((__m128i*)(d + i), _mm_xor_si128(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] ^ pb[i];
}

static void sse42_bit_andnot(void* dst, const void* a, const void* b,
    size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(pa + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(pb + i));
        _mm_storeu_si128((__m128i*)(d + i), _mm_andnot_si128(vb, va));
    }
    for (; i < n; i++) d[i] = pa[i] & ~pb[i];
}

/* ====================================================================
 * Hashing
 * ==================================================================== */

static uint64_t sse42_hash64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    return x;
}

static uint64_t sse42_hash64_buf(const uint64_t* buf, size_t count) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)count;
    for (size_t i = 0; i < count; i++) {
        h = sse42_hash64(h ^ buf[i]);
    }
    return h;
}

/* ====================================================================
 * Backend constructor
 * ==================================================================== */

static const struct simd_backend g_sse42_backend = {
    .isa = SIMD_ISA_SSE42,

    .fn_memcpy = sse42_memcpy,
    .fn_memmove = sse42_memmove,
    .fn_memset = sse42_memset,
    .fn_memcmp = sse42_memcmp,
    .fn_memchr = sse42_memchr,

    .fn_find_u64 = sse42_find_u64,
    .fn_find_u32 = sse42_find_u32,
    .fn_find_u16 = sse42_find_u16,
    .fn_find_u8 = sse42_find_u8,

    .fn_cmp_u64 = sse42_cmp_u64,
    .fn_cmp_u32 = sse42_cmp_u32,
    .fn_cmp_u8 = sse42_cmp_u8,

    .fn_popcount = sse42_popcount,
    .fn_bit_and = sse42_bit_and,
    .fn_bit_or = sse42_bit_or,
    .fn_bit_xor = sse42_bit_xor,
    .fn_bit_andnot = sse42_bit_andnot,

    .fn_hash64 = sse42_hash64,
    .fn_hash64_buf = sse42_hash64_buf,
};

const struct simd_backend* simd_backend_sse42(void) {
    return &g_sse42_backend;
}

#endif  /* x86-64 */