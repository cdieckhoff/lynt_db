/**
 * @file simd_neon.c
 * @brief ARM64 NEON implementation of the SIMD API.
 *
 * Compiled only on aarch64 / ARM64 targets. NEON is guaranteed on
 * ARMv8-A, so no runtime detection is needed beyond the build-time
 * architecture check.
 *
 * Each primitive processes 128-bit vectors: 2 u64, 4 u32, 8 u16, or
 * 16 u8 per iteration. Similar throughput to SSE4.2 on x86.
 *
 * PORTABILITY NOTES
 * -----------------
 * Only compiled on ARM64. The <arm_neon.h> header is provided by every
 * compiler that targets ARM64: GCC, Clang, and MSVC's ARM64 toolchain.
 * The intrinsics used here are the base NEON set, not the optional
 * crypto or dot-product extensions, so no feature detection is needed.
 *
 * No __builtin_popcount. Clang supports it on ARM, but MSVC's ARM64
 * compiler does not, so we use a bit-twiddling helper for portability.
 */

#include "simd/simd_internal.h"

#if defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>
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

static void neon_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

static void neon_memmove(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

static void neon_memset(void* dst, int value, size_t n) {
    memset(dst, value, n);
}

static int neon_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}

static void* neon_memchr(const void* buf, int c, size_t n) {
    return memchr(buf, c, n);
}

/* ====================================================================
 * Search primitives
 * ==================================================================== */

static uint64_t neon_find_u8(const uint8_t* buf, uint8_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    uint8x16_t tgt = vdupq_n_u8(target);

    for (; i + 16 <= n; i += 16) {
        uint8x16_t v = vld1q_u8(buf + i);
        uint8x16_t eq = vceqq_u8(v, tgt);
        uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(eq), 4);
        uint64_t  bits = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
        for (int b = 0; b < 8; b++) {
            if ((bits >> (b * 8)) & 0x0F) mask |= (uint64_t)1 << (i + b);
        }
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t neon_find_u64(const uint64_t* buf, uint64_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    uint32x4_t tgt = vdupq_n_u32((uint32_t)target);

    for (; i + 2 <= n; i += 2) {
        uint32x4_t v = vld1q_u32((const uint32_t*)(buf + i));
        uint32x4_t eq = vceqq_u32(v, tgt);
        uint64_t bits0 = vgetq_lane_u64(vreinterpretq_u64_u32(eq), 0);
        uint64_t bits1 = vgetq_lane_u64(vreinterpretq_u64_u32(eq), 1);
        if (bits0 == 0xFFFFFFFFFFFFFFFFULL) mask |= (uint64_t)1 << i;
        if (bits1 == 0xFFFFFFFFFFFFFFFFULL) mask |= (uint64_t)1 << (i + 1);
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t neon_find_u32(const uint32_t* buf, uint32_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    uint32x4_t tgt = vdupq_n_u32(target);

    for (; i + 4 <= n; i += 4) {
        uint32x4_t v = vld1q_u32(buf + i);
        uint32x4_t eq = vceqq_u32(v, tgt);
        uint32x4_t shifted = vshrq_n_u32(eq, 31);
        uint64_t bits = vgetq_lane_u64(vreinterpretq_u64_u32(shifted), 0);
        for (int b = 0; b < 4; b++) {
            if ((bits >> (b * 16)) & 1) mask |= (uint64_t)1 << (i + b);
        }
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t neon_find_u16(const uint16_t* buf, uint16_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    uint16x8_t tgt = vdupq_n_u16(target);

    for (; i + 8 <= n; i += 8) {
        uint16x8_t v = vld1q_u16(buf + i);
        uint16x8_t eq = vceqq_u16(v, tgt);
        uint8x8_t narrowed = vshrn_n_u16(eq, 8);
        uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
        for (int b = 0; b < 8; b++) {
            if ((bits >> (b * 8)) & 1) mask |= (uint64_t)1 << (i + b);
        }
    }

    for (; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Comparison primitives
 * ==================================================================== */

static uint64_t neon_cmp_u8(const uint8_t* a, const uint8_t* b, size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(a + i);
        uint8x16_t vb = vld1q_u8(b + i);
        uint8x16_t eq = vceqq_u8(va, vb);
        uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(eq), 4);
        uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
        for (int k = 0; k < 8; k++) {
            if ((bits >> (k * 8)) & 0x0F) mask |= (uint64_t)1 << (i + k);
        }
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t neon_cmp_u32(const uint32_t* a, const uint32_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 4 <= n; i += 4) {
        uint32x4_t va = vld1q_u32(a + i);
        uint32x4_t vb = vld1q_u32(b + i);
        uint32x4_t eq = vceqq_u32(va, vb);
        uint32x4_t shifted = vshrq_n_u32(eq, 31);
        uint64_t bits = vgetq_lane_u64(vreinterpretq_u64_u32(shifted), 0);
        for (int k = 0; k < 4; k++) {
            if ((bits >> (k * 16)) & 1) mask |= (uint64_t)1 << (i + k);
        }
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t neon_cmp_u64(const uint64_t* a, const uint64_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 2 <= n; i += 2) {
        uint32x4_t va = vld1q_u32((const uint32_t*)(a + i));
        uint32x4_t vb = vld1q_u32((const uint32_t*)(b + i));
        uint32x4_t eq = vceqq_u32(va, vb);
        uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u32(eq), 0);
        uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u32(eq), 1);
        if (lo == 0xFFFFFFFFFFFFFFFFULL) mask |= (uint64_t)1 << i;
        if (hi == 0xFFFFFFFFFFFFFFFFULL) mask |= (uint64_t)1 << (i + 1);
    }

    for (; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Bit operations
 * ==================================================================== */

static uint64_t neon_popcount(const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    uint64_t total = 0;
    size_t i = 0;

    uint8x16_t acc = vdupq_n_u8(0);
    static const uint8_t lookup_arr[16] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
    uint8x16_t lookup = vld1q_u8(lookup_arr);
    uint8x16_t low_mask = vdupq_n_u8(0x0F);

    for (; i + 16 <= n; i += 16) {
        uint8x16_t v = vld1q_u8(p + i);
        uint8x16_t lo = vandq_u8(v, low_mask);
        uint8x16_t hi = vandq_u8(vshrq_n_u8(v, 4), low_mask);
        uint8x16_t pcnt = vaddq_u8(vqtbl1q_u8(lookup, lo),
            vqtbl1q_u8(lookup, hi));
        acc = vaddq_u8(acc, pcnt);
    }

    total += (uint64_t)vaddvq_u8(acc);

    for (; i < n; i++) {
        total += (uint64_t)simd_popcount8(p[i]);
    }
    return total;
}

static void neon_bit_and(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(pa + i);
        uint8x16_t vb = vld1q_u8(pb + i);
        vst1q_u8(d + i, vandq_u8(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] & pb[i];
}

static void neon_bit_or(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(pa + i);
        uint8x16_t vb = vld1q_u8(pb + i);
        vst1q_u8(d + i, vorrq_u8(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] | pb[i];
}

static void neon_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(pa + i);
        uint8x16_t vb = vld1q_u8(pb + i);
        vst1q_u8(d + i, veorq_u8(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] ^ pb[i];
}

static void neon_bit_andnot(void* dst, const void* a, const void* b,
    size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(pa + i);
        uint8x16_t vb = vld1q_u8(pb + i);
        vst1q_u8(d + i, vbicq_u8(va, vb));
    }
    for (; i < n; i++) d[i] = pa[i] & ~pb[i];
}

/* ====================================================================
 * Hashing — identical to scalar
 * ==================================================================== */

static uint64_t neon_hash64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    return x;
}

static uint64_t neon_hash64_buf(const uint64_t* buf, size_t count) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)count;
    for (size_t i = 0; i < count; i++) {
        h = neon_hash64(h ^ buf[i]);
    }
    return h;
}

/* ====================================================================
 * Backend constructor
 * ==================================================================== */

static const struct simd_backend g_neon_backend = {
    .isa = SIMD_ISA_NEON,

    .fn_memcpy = neon_memcpy,
    .fn_memmove = neon_memmove,
    .fn_memset = neon_memset,
    .fn_memcmp = neon_memcmp,
    .fn_memchr = neon_memchr,

    .fn_find_u64 = neon_find_u64,
    .fn_find_u32 = neon_find_u32,
    .fn_find_u16 = neon_find_u16,
    .fn_find_u8 = neon_find_u8,

    .fn_cmp_u64 = neon_cmp_u64,
    .fn_cmp_u32 = neon_cmp_u32,
    .fn_cmp_u8 = neon_cmp_u8,

    .fn_popcount = neon_popcount,
    .fn_bit_and = neon_bit_and,
    .fn_bit_or = neon_bit_or,
    .fn_bit_xor = neon_bit_xor,
    .fn_bit_andnot = neon_bit_andnot,

    .fn_hash64 = neon_hash64,
    .fn_hash64_buf = neon_hash64_buf,
};

const struct simd_backend* simd_backend_neon(void) {
    return &g_neon_backend;
}

#endif  /* ARM64 */