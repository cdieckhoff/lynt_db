/**
 * @file simd_neon.c
 * @brief ARM64 NEON implementation of the SIMD API.
 *
 * Compiled only on aarch64 / ARM64 targets.
 *
 * MASK EXTRACTION
 * ---------------
 * NEON has no movemask instruction. Extracting a scalar bitmask from
 * a vector comparison requires reducing 0xFF/0x00 bytes to bits. The
 * helper functions below do that with basic NEON operations only —
 * no vshrn (GCC 13 aarch64 rejects constant shift arguments inside
 * inline helpers) and no vqtbl (MSVC ARM64 has shown incorrect code
 * generation on the Windows ARM runner).
 *
 * PORTABILITY NOTES
 * -----------------
 * - The 64-bit compare builds its target vector as {lo, hi, lo, hi}
 *   so each 64-bit lane of the vector matches the full target. Using
 *   vdupq_n_u32(target) only sets the low 32 bits, which is why an
 *   earlier version returned zero for find_u64 and cmp_u64.
 *
 * - popcount uses vector SWAR rather than a nibble-lookup table. The
 *   lookup approach relies on vqtbl1q_u8, which is available on
 *   ARMv8-A but has produced incorrect results on some toolchains.
 *
 * - The horizontal sum in popcount widens uint8_t lanes to uint16_t
 *   before reducing. vaddvq_u8 returns uint8_t and wraps modulo 256
 *   when the total exceeds 255, which happens for any buffer longer
 *   than about 64 bytes. Widening to u16 keeps the sum exact.
 */

#include "simd/simd_internal.h"

#if defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>
#include <string.h>

 /* ====================================================================
  * Portable bit helpers (for the scalar tail)
  * ==================================================================== */

static inline int simd_popcount8(uint8_t v) {
    v = (uint8_t)(v - ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (int)((v + (v >> 4)) & 0x0F);
}

/* ====================================================================
 * Mask extraction helpers
 * ==================================================================== */

static inline uint16_t neon_mask_u8(uint8x16_t eq) {
    uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(eq), 0);
    uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(eq), 1);

    uint16_t mask = 0;
    for (int i = 0; i < 8; i++) {
        if ((lo >> (i * 8)) & 0x01) mask |= (uint16_t)1 << i;
    }
    for (int i = 0; i < 8; i++) {
        if ((hi >> (i * 8)) & 0x01) mask |= (uint16_t)1 << (i + 8);
    }
    return mask;
}

static inline uint32_t neon_mask_u32(uint32x4_t eq) {
    uint32_t a0 = vgetq_lane_u32(eq, 0) & 1;
    uint32_t a1 = vgetq_lane_u32(eq, 1) & 1;
    uint32_t a2 = vgetq_lane_u32(eq, 2) & 1;
    uint32_t a3 = vgetq_lane_u32(eq, 3) & 1;
    return a0 | (a1 << 1) | (a2 << 2) | (a3 << 3);
}

static inline uint32_t neon_mask_u16(uint16x8_t eq) {
    uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u16(eq), 0);
    uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u16(eq), 1);

    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        if ((lo >> (i * 16)) & 0xFFFF) mask |= 1u << i;
    }
    for (int i = 0; i < 4; i++) {
        if ((hi >> (i * 16)) & 0xFFFF) mask |= 1u << (i + 4);
    }
    return mask;
}

static inline uint32_t neon_mask_u64_pair(uint32x4_t eq_u32) {
    uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u32(eq_u32), 0);
    uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u32(eq_u32), 1);

    uint32_t mask = 0;
    if (lo == 0xFFFFFFFFFFFFFFFFULL) mask |= 1u;
    if (hi == 0xFFFFFFFFFFFFFFFFULL) mask |= 2u;
    return mask;
}

/**
 * @brief Build a 4-lane uint32 vector holding {lo, hi, lo, hi} of a
 *        64-bit value, so each 64-bit lane of the vector equals the
 *        full value.
 *
 * This is what a 64-bit compare against a scalar needs. Using
 * vdupq_n_u32(x) only sets the low 32 bits, which produces no matches
 * on the high half of the comparison and returns zero.
 */
static inline uint32x4_t neon_u64_to_vec(uint64_t v) {
    uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t data[4] = { lo, hi, lo, hi };
    return vld1q_u32(data);
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
        uint16_t bits = neon_mask_u8(eq);
        mask |= (uint64_t)bits << i;
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

    uint32x4_t tgt = neon_u64_to_vec(target);

    for (; i + 2 <= n; i += 2) {
        uint32x4_t v = vld1q_u32((const uint32_t*)(buf + i));
        uint32x4_t eq = vceqq_u32(v, tgt);
        uint32_t bits = neon_mask_u64_pair(eq);
        mask |= (uint64_t)bits << i;
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
        uint32_t bits = neon_mask_u32(eq);
        mask |= (uint64_t)bits << i;
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
        uint32_t bits = neon_mask_u16(eq);
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

static uint64_t neon_cmp_u8(const uint8_t* a, const uint8_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t i = 0;
    size_t n = count < 64 ? count : 64;

    for (; i + 16 <= n; i += 16) {
        uint8x16_t va = vld1q_u8(a + i);
        uint8x16_t vb = vld1q_u8(b + i);
        uint8x16_t eq = vceqq_u8(va, vb);
        uint16_t bits = neon_mask_u8(eq);
        mask |= (uint64_t)bits << i;
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
        uint32_t bits = neon_mask_u32(eq);
        mask |= (uint64_t)bits << i;
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
        uint32_t bits = neon_mask_u64_pair(eq);
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

static uint64_t neon_popcount(const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    uint64_t total = 0;
    size_t i = 0;

    /* Vector SWAR popcount: three shift-and-mask steps on 16 bytes at
     * a time, then accumulate. The intermediate values fit in uint8_t
     * because the maximum popcount of one byte is 8, and the
     * accumulator lane can hold up to 255 before wrapping. */
    uint8x16_t acc = vdupq_n_u8(0);
    uint8x16_t mask55 = vdupq_n_u8(0x55);
    uint8x16_t mask33 = vdupq_n_u8(0x33);
    uint8x16_t mask0F = vdupq_n_u8(0x0F);

    for (; i + 16 <= n; i += 16) {
        uint8x16_t v = vld1q_u8(p + i);
        uint8x16_t v1 = vsubq_u8(v, vandq_u8(vshrq_n_u8(v, 1), mask55));
        uint8x16_t v2 = vaddq_u8(vandq_u8(v1, mask33),
            vandq_u8(vshrq_n_u8(v1, 2), mask33));
        uint8x16_t v3 = vandq_u8(vaddq_u8(v2, vshrq_n_u8(v2, 4)), mask0F);
        acc = vaddq_u8(acc, v3);
    }

    /* Horizontal sum: widen to 16-bit lanes before adding across the
     * vector. vaddvq_u8 returns uint8_t and wraps modulo 256 when the
     * total exceeds 255, which happens for any buffer longer than
     * about 64 bytes. Widening to u16 keeps the sum exact. */
    uint16x8_t lo16 = vmovl_u8(vget_low_u8(acc));
    uint16x8_t hi16 = vmovl_u8(vget_high_u8(acc));
    uint16x8_t sum16 = vaddq_u16(lo16, hi16);
    total += (uint64_t)vaddvq_u16(sum16);

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
        vst1q_u8(d + i, vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i)));
    }
    for (; i < n; i++) d[i] = pa[i] & pb[i];
}

static void neon_bit_or(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        vst1q_u8(d + i, vorrq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i)));
    }
    for (; i < n; i++) d[i] = pa[i] | pb[i];
}

static void neon_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        vst1q_u8(d + i, veorq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i)));
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
        vst1q_u8(d + i, vbicq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i)));
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