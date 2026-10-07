/**
 * @file simd_api.c
 * @brief Public API entry points.
 *
 * Every function here is a one-line forwarder through the dispatch
 * table. The simd_init() call is cheap after the first invocation.
 * Keeping it in each forwarder means callers never have to remember to
 * init explicitly.
 */

#include "simd/simd_internal.h"

 /* ====================================================================
  * Memory primitives
  * ==================================================================== */

void simd_memcpy(void* dst, const void* src, size_t n) {
    simd_init();
    g_simd.fn_memcpy(dst, src, n);
}

void simd_memmove(void* dst, const void* src, size_t n) {
    simd_init();
    g_simd.fn_memmove(dst, src, n);
}

void simd_memset(void* dst, int value, size_t n) {
    simd_init();
    g_simd.fn_memset(dst, value, n);
}

int simd_memcmp(const void* a, const void* b, size_t n) {
    simd_init();
    return g_simd.fn_memcmp(a, b, n);
}

void* simd_memchr(const void* buf, int c, size_t n) {
    simd_init();
    return g_simd.fn_memchr(buf, c, n);
}

/* ====================================================================
 * Search primitives
 * ==================================================================== */

uint64_t simd_find_u64(const uint64_t* buf, uint64_t target, size_t count) {
    simd_init();
    return g_simd.fn_find_u64(buf, target, count);
}

uint64_t simd_find_u32(const uint32_t* buf, uint32_t target, size_t count) {
    simd_init();
    return g_simd.fn_find_u32(buf, target, count);
}

uint64_t simd_find_u16(const uint16_t* buf, uint16_t target, size_t count) {
    simd_init();
    return g_simd.fn_find_u16(buf, target, count);
}

uint64_t simd_find_u8(const uint8_t* buf, uint8_t target, size_t count) {
    simd_init();
    return g_simd.fn_find_u8(buf, target, count);
}

/* ====================================================================
 * Comparison primitives
 * ==================================================================== */

uint64_t simd_cmp_u64(const uint64_t* a, const uint64_t* b, size_t count) {
    simd_init();
    return g_simd.fn_cmp_u64(a, b, count);
}

uint64_t simd_cmp_u32(const uint32_t* a, const uint32_t* b, size_t count) {
    simd_init();
    return g_simd.fn_cmp_u32(a, b, count);
}

uint64_t simd_cmp_u8(const uint8_t* a, const uint8_t* b, size_t count) {
    simd_init();
    return g_simd.fn_cmp_u8(a, b, count);
}

/* ====================================================================
 * Bit operations
 * ==================================================================== */

uint64_t simd_popcount(const void* buf, size_t n) {
    simd_init();
    return g_simd.fn_popcount(buf, n);
}

void simd_bit_and(void* dst, const void* a, const void* b, size_t n) {
    simd_init();
    g_simd.fn_bit_and(dst, a, b, n);
}

void simd_bit_or(void* dst, const void* a, const void* b, size_t n) {
    simd_init();
    g_simd.fn_bit_or(dst, a, b, n);
}

void simd_bit_xor(void* dst, const void* a, const void* b, size_t n) {
    simd_init();
    g_simd.fn_bit_xor(dst, a, b, n);
}

void simd_bit_andnot(void* dst, const void* a, const void* b, size_t n) {
    simd_init();
    g_simd.fn_bit_andnot(dst, a, b, n);
}

/* ====================================================================
 * Hashing
 * ==================================================================== */

uint64_t simd_hash64(uint64_t x) {
    simd_init();
    return g_simd.fn_hash64(x);
}

uint64_t simd_hash64_buf(const uint64_t* buf, size_t count) {
    simd_init();
    return g_simd.fn_hash64_buf(buf, count);
}