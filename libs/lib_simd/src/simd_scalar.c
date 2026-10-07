/**
 * @file simd_scalar.c
 * @brief Portable C implementation of the SIMD API.
 *
 * This is the reference implementation. Every other backend must
 * produce byte-identical results to this one. Tests compare them
 * against this backend, and when a mismatch is found, this is the
 * source of truth.
 *
 * No intrinsics. No ISA assumptions. Compiles on any conforming C11
 * compiler for any architecture. If a target CPU doesn't support any
 * SIMD extensions, this is what runs.
 *
 * Performance is not the goal here. Correctness is.
 *
 * PORTABILITY NOTES
 * -----------------
 * The file avoids every compiler-specific extension:
 *
 *   - No __builtin_popcount. MSVC doesn't have it. We use a
 *     bit-twiddling popcount that works everywhere.
 *
 *   - No __builtin_ctz. Same reason. Not used here anyway.
 *
 *   - No <stdatomic.h>. MSVC gates it behind /experimental:c11atomics,
 *     which is unstable. Not used here.
 *
 * The only headers included are the standard C library and the
 * library's own simd.h.
 */

#include "simd/simd_internal.h"
#include <string.h>

 /* ====================================================================
  * Portable bit helpers
  * ==================================================================== */

  /**
   * @brief Count the number of set bits in a single byte.
   *
   * Classic bit-twiddling popcount. Compiles to a handful of
   * instructions on every compiler and produces correct results on
   * every architecture. MSVC doesn't provide __builtin_popcount, so we
   * use this instead of a compiler-specific builtin.
   *
   * Only used in the scalar tail of the popcount loop, where the cost
   * is negligible. The main loop handles 32 bytes at a time via SIMD in
   * the accelerated backends; this is just cleanup.
   */
static inline int simd_popcount8(uint8_t v) {
    v = (uint8_t)(v - ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (int)((v + (v >> 4)) & 0x0F);
}

/* ====================================================================
 * Memory primitives
 * ==================================================================== */

static void scalar_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

static void scalar_memmove(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

static void scalar_memset(void* dst, int value, size_t n) {
    memset(dst, value, n);
}

static int scalar_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}

static void* scalar_memchr(const void* buf, int c, size_t n) {
    return memchr(buf, c, n);
}

/* ====================================================================
 * Search primitives
 *
 * Each builds a 64-bit mask. Only the low `count` bits are meaningful.
 * ==================================================================== */

static uint64_t scalar_find_u64(const uint64_t* buf, uint64_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t scalar_find_u32(const uint32_t* buf, uint32_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t scalar_find_u16(const uint16_t* buf, uint16_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t scalar_find_u8(const uint8_t* buf, uint8_t target,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == target) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Comparison primitives
 * ==================================================================== */

static uint64_t scalar_cmp_u64(const uint64_t* a, const uint64_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t scalar_cmp_u32(const uint32_t* a, const uint32_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

static uint64_t scalar_cmp_u8(const uint8_t* a, const uint8_t* b,
    size_t count) {
    uint64_t mask = 0;
    size_t n = count < 64 ? count : 64;
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) mask |= (uint64_t)1 << i;
    }
    return mask;
}

/* ====================================================================
 * Bit operations
 * ==================================================================== */

static uint64_t scalar_popcount(const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    uint64_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += (uint64_t)simd_popcount8(p[i]);
    }
    return total;
}

static void scalar_bit_and(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) d[i] = pa[i] & pb[i];
}

static void scalar_bit_or(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) d[i] = pa[i] | pb[i];
}

static void scalar_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) d[i] = pa[i] ^ pb[i];
}

static void scalar_bit_andnot(void* dst, const void* a, const void* b,
    size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) d[i] = pa[i] & ~pb[i];
}

/* ====================================================================
 * Hashing — splitmix64 finalizer
 * ==================================================================== */

static uint64_t scalar_hash64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    return x;
}

static uint64_t scalar_hash64_buf(const uint64_t* buf, size_t count) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)count;
    for (size_t i = 0; i < count; i++) {
        h = scalar_hash64(h ^ buf[i]);
    }
    return h;
}

/* ====================================================================
 * Backend constructor
 * ==================================================================== */

static const struct simd_backend g_scalar_backend = {
    .isa = SIMD_ISA_SCALAR,

    .fn_memcpy = scalar_memcpy,
    .fn_memmove = scalar_memmove,
    .fn_memset = scalar_memset,
    .fn_memcmp = scalar_memcmp,
    .fn_memchr = scalar_memchr,

    .fn_find_u64 = scalar_find_u64,
    .fn_find_u32 = scalar_find_u32,
    .fn_find_u16 = scalar_find_u16,
    .fn_find_u8 = scalar_find_u8,

    .fn_cmp_u64 = scalar_cmp_u64,
    .fn_cmp_u32 = scalar_cmp_u32,
    .fn_cmp_u8 = scalar_cmp_u8,

    .fn_popcount = scalar_popcount,
    .fn_bit_and = scalar_bit_and,
    .fn_bit_or = scalar_bit_or,
    .fn_bit_xor = scalar_bit_xor,
    .fn_bit_andnot = scalar_bit_andnot,

    .fn_hash64 = scalar_hash64,
    .fn_hash64_buf = scalar_hash64_buf,
};

const struct simd_backend* simd_backend_scalar(void) {
    return &g_scalar_backend;
}