/**
 * @file simd_internal.h
 * @brief Private declarations shared by the SIMD backend files.
 *
 * Not installed. Only the library sources and tests/test_simd.c
 * include this.
 * 
 * THE DISPATCH MODEL
 * ------------------
 * The library exposes a single public API in simd/simd.h. Behind that
 * API, four backends implement the same set of operations:
 *
 *   - scalar:  portable C, correct on any CPU
 *   - sse42:   x86-64 SSE4.2 (baseline for modern x86)
 *   - avx2:    x86-64 AVX2 + FMA (the fast path on most modern x86)
 *   - neon:    ARM64 NEON
 *
 * Each backend defines a struct simd_backend containing function
 * pointers to its implementations. At init time, simd_init() picks the
 * best available backend and fills a global dispatch table with its
 * pointers. Every public API function is a one-line forwarder that
 * calls through the table.
 *
 * WHY THE fn_ PREFIX ON MEMBERS
 * -----------------------------
 * The struct members are named fn_memcpy, fn_memmove, etc. rather than
 * memcpy, memmove, etc. MSVC rejects struct member names that collide
 * with C standard library function names visible at the point of
 * declaration, even though the C standard permits the shadowing. GCC
 * and Clang accept the collision; MSVC refuses it with "redefinition".
 * The fn_ prefix avoids the collision on all compilers with zero
 * runtime cost and makes it obvious at a glance that the member is a
 * function pointer, not a function.
 */

#ifndef SIMD_INTERNAL_H
#define SIMD_INTERNAL_H

#include "simd.h"
#include <stddef.h>
#include <stdint.h>

 /* ====================================================================
  * Backend structure
  *
  * Each backend provides this struct, populated with pointers to its
  * functions. simd_init() copies the selected backend's struct into the
  * global dispatch table.
  * ==================================================================== */

struct simd_backend {
    simd_isa_t isa;

    /* Memory primitives */
    void     (*fn_memcpy)(void*, const void*, size_t);
    void     (*fn_memmove)(void*, const void*, size_t);
    void     (*fn_memset)(void*, int, size_t);
    int      (*fn_memcmp)(const void*, const void*, size_t);
    void* (*fn_memchr)(const void*, int, size_t);

    /* Search primitives */
    uint64_t(*fn_find_u64)(const uint64_t*, uint64_t, size_t);
    uint64_t(*fn_find_u32)(const uint32_t*, uint32_t, size_t);
    uint64_t(*fn_find_u16)(const uint16_t*, uint16_t, size_t);
    uint64_t(*fn_find_u8)(const uint8_t*, uint8_t, size_t);

    /* Comparison primitives */
    uint64_t(*fn_cmp_u64)(const uint64_t*, const uint64_t*, size_t);
    uint64_t(*fn_cmp_u32)(const uint32_t*, const uint32_t*, size_t);
    uint64_t(*fn_cmp_u8)(const uint8_t*, const uint8_t*, size_t);

    /* Bit operations */
    uint64_t(*fn_popcount)(const void*, size_t);
    void     (*fn_bit_and)(void*, const void*, const void*, size_t);
    void     (*fn_bit_or)(void*, const void*, const void*, size_t);
    void     (*fn_bit_xor)(void*, const void*, const void*, size_t);
    void     (*fn_bit_andnot)(void*, const void*, const void*, size_t);

    /* Hashing */
    uint64_t(*fn_hash64)(uint64_t);
    uint64_t(*fn_hash64_buf)(const uint64_t*, size_t);
};

/* ====================================================================
 * Backend constructors
 *
 * Each returns a struct simd_backend for its ISA level. The scalar
 * backend is always available; the others are compiled only on
 * platforms that support them.
 * ==================================================================== */

const struct simd_backend* simd_backend_scalar(void);

#if defined(__x86_64__) || defined(_M_X64)
const struct simd_backend* simd_backend_sse42(void);
const struct simd_backend* simd_backend_avx2(void);
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
const struct simd_backend* simd_backend_neon(void);
#endif

/* ====================================================================
 * The global dispatch table
 *
 * Filled exactly once by simd_init(). Read-only thereafter.
 * ==================================================================== */

extern struct simd_backend g_simd;

#endif /* SIMD_INTERNAL_H */