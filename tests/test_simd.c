/**
 * @file test_simd.c
 * @brief Correctness tests for the SIMD library.
 *
 * STRATEGY
 * --------
 * Every backend must produce byte-identical output to the scalar
 * reference. Tests exercise each public API function, then call the
 * backend function pointers directly (bypassing dispatch) and assert
 * equality between every available backend.
 *
 * The tests also exercise edge cases: buffers of length 0, 1, 7, 63,
 * 64, 65, and other boundaries. Most SIMD bugs live at these sizes
 * where the vector loop falls through to the scalar tail.
 */

#include "test_harness.h"
#include "simd/simd.h"
#include "simd/simd_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

 /* ====================================================================
  * Helpers
  * ==================================================================== */

static uint64_t g_rng_state = 0x853C49E6748FEA9BULL;

static uint64_t rng_next(void) {
    uint64_t x = g_rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void fill_random_u64(uint64_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = rng_next();
}

static void fill_random_u8(uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)rng_next();
}

struct backend_list {
    const struct simd_backend* b[4];
    int count;
};

static struct backend_list get_all_backends(void) {
    struct backend_list list = { .count = 0 };
    list.b[list.count++] = simd_backend_scalar();

#if defined(__x86_64__) || defined(_M_X64)
    list.b[list.count++] = simd_backend_sse42();
    list.b[list.count++] = simd_backend_avx2();
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    list.b[list.count++] = simd_backend_neon();
#endif
    return list;
}

/* ====================================================================
 * TEST: initialization
 * ==================================================================== */

static void test_init(void) {
    simd_isa_t isa = simd_init();
    printf("      active ISA: %s\n", simd_isa_name(isa));

    TEST_ASSERT(simd_active_isa() == isa);

    TEST_ASSERT(simd_isa_name(SIMD_ISA_SCALAR) != NULL);
    TEST_ASSERT(simd_isa_name(SIMD_ISA_SSE42) != NULL);
    TEST_ASSERT(simd_isa_name(SIMD_ISA_AVX2) != NULL);
    TEST_ASSERT(simd_isa_name(SIMD_ISA_NEON) != NULL);
}

/* ====================================================================
 * TEST: memory primitives
 * ==================================================================== */

static void test_memcpy_all(void) {
    struct backend_list bl = get_all_backends();

    uint8_t src[257];
    uint8_t dst_a[257];
    uint8_t dst_b[257];
    fill_random_u8(src, sizeof(src));

    for (int b = 0; b < bl.count; b++) {
        for (size_t n = 0; n <= 257; n += 13) {
            memset(dst_a, 0xCC, sizeof(dst_a));
            memset(dst_b, 0xCC, sizeof(dst_b));

            bl.b[b]->fn_memcpy(dst_a, src, n);
            memcpy(dst_b, src, n);

            TEST_ASSERT(memcmp(dst_a, dst_b, n) == 0);
        }
    }
}

static void test_memset_all(void) {
    struct backend_list bl = get_all_backends();

    for (int b = 0; b < bl.count; b++) {
        for (size_t n = 0; n <= 128; n += 7) {
            uint8_t dst_a[128];
            uint8_t dst_b[128];
            memset(dst_a, 0xAA, sizeof(dst_a));
            memset(dst_b, 0xAA, sizeof(dst_b));

            bl.b[b]->fn_memset(dst_a, 0x5A, n);
            memset(dst_b, 0x5A, n);

            TEST_ASSERT(memcmp(dst_a, dst_b, sizeof(dst_a)) == 0);
        }
    }
}

static void test_memcmp_all(void) {
    struct backend_list bl = get_all_backends();

    uint8_t a[64];
    uint8_t b[64];

    fill_random_u8(a, sizeof(a));
    memcpy(b, a, sizeof(a));
    for (int i = 0; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_memcmp(a, b, 64) == 0);
    }

    b[30] ^= 0xFF;
    for (int i = 0; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_memcmp(a, b, 64) != 0);
    }
}

/* ====================================================================
 * TEST: search primitives
 * ==================================================================== */

static void test_find_u64_all(void) {
    struct backend_list bl = get_all_backends();

    uint64_t buf[128];
    fill_random_u64(buf, 128);

    uint64_t target = 0xCAFEBABEDEADBEEFULL;
    buf[3] = target;
    buf[17] = target;
    buf[63] = target;
    buf[127] = target;

    size_t count = 64;
    uint64_t reference = bl.b[0]->fn_find_u64(buf, target, count);
    printf("      find_u64 mask: 0x%016llx\n",
        (unsigned long long)reference);

    for (int b = 1; b < bl.count; b++) {
        uint64_t m = bl.b[b]->fn_find_u64(buf, target, count);
        if (m != reference) {
            printf("      backend %d disagrees: 0x%016llx vs 0x%016llx\n",
                b, (unsigned long long)m, (unsigned long long)reference);
        }
        TEST_ASSERT(m == reference);
    }

    TEST_ASSERT(simd_find_u64(buf, target, count) == reference);
}

static void test_find_u32_all(void) {
    struct backend_list bl = get_all_backends();

    uint32_t buf[128];
    for (size_t i = 0; i < 128; i++) buf[i] = (uint32_t)rng_next();

    uint32_t target = 0xDEADBEEF;
    buf[5] = target;
    buf[31] = target;
    buf[62] = target;

    size_t count = 64;
    uint64_t reference = bl.b[0]->fn_find_u32(buf, target, count);
    for (int b = 1; b < bl.count; b++) {
        TEST_ASSERT(bl.b[b]->fn_find_u32(buf, target, count) == reference);
    }
    TEST_ASSERT(simd_find_u32(buf, target, count) == reference);
}

static void test_find_u16_all(void) {
    struct backend_list bl = get_all_backends();

    uint16_t buf[128];
    for (size_t i = 0; i < 128; i++) buf[i] = (uint16_t)rng_next();

    uint16_t target = 0xABCD;
    buf[7] = target;
    buf[50] = target;

    size_t count = 64;
    uint64_t reference = bl.b[0]->fn_find_u16(buf, target, count);
    for (int b = 1; b < bl.count; b++) {
        TEST_ASSERT(bl.b[b]->fn_find_u16(buf, target, count) == reference);
    }
    TEST_ASSERT(simd_find_u16(buf, target, count) == reference);
}

static void test_find_u8_all(void) {
    struct backend_list bl = get_all_backends();

    uint8_t buf[128];
    fill_random_u8(buf, 128);
    for (size_t i = 0; i < 128; i++) buf[i] |= 0x01;

    uint8_t target = 0xF0;
    buf[0] = target;
    buf[15] = target;
    buf[32] = target;
    buf[63] = target;

    size_t count = 64;
    uint64_t reference = bl.b[0]->fn_find_u8(buf, target, count);
    printf("      find_u8 mask:  0x%016llx\n",
        (unsigned long long)reference);
    for (int b = 1; b < bl.count; b++) {
        uint64_t m = bl.b[b]->fn_find_u8(buf, target, count);
        if (m != reference) {
            printf("      backend %d disagrees: 0x%016llx vs 0x%016llx\n",
                b, (unsigned long long)m, (unsigned long long)reference);
        }
        TEST_ASSERT(m == reference);
    }
    TEST_ASSERT(simd_find_u8(buf, target, count) == reference);
}

static void test_cmp_all(void) {
    struct backend_list bl = get_all_backends();

    uint64_t a[64];
    uint64_t b[64];
    fill_random_u64(a, 64);
    memcpy(b, a, sizeof(a));

    b[3] = ~b[3];
    b[20] = ~b[20];
    b[63] = ~b[63];

    uint64_t ref64 = bl.b[0]->fn_cmp_u64(a, b, 64);
    for (int i = 1; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_cmp_u64(a, b, 64) == ref64);
    }
    TEST_ASSERT(simd_cmp_u64(a, b, 64) == ref64);

    uint8_t c[64];
    uint8_t d[64];
    fill_random_u8(c, 64);
    memcpy(d, c, 64);
    d[0] ^= 1;
    d[32] ^= 1;

    uint64_t ref8 = bl.b[0]->fn_cmp_u8(c, d, 64);
    for (int i = 1; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_cmp_u8(c, d, 64) == ref8);
    }
}

/* ====================================================================
 * TEST: popcount and bitwise ops
 * ==================================================================== */

static void test_popcount_all(void) {
    struct backend_list bl = get_all_backends();

    uint8_t buf[257];
    fill_random_u8(buf, sizeof(buf));

    uint64_t reference = bl.b[0]->fn_popcount(buf, sizeof(buf));
    printf("      popcount(257 bytes) = %llu\n",
        (unsigned long long)reference);

    for (int i = 1; i < bl.count; i++) {
        uint64_t got = bl.b[i]->fn_popcount(buf, sizeof(buf));
        if (got != reference) {
            printf("      backend %d disagrees: %llu vs %llu\n",
                i, (unsigned long long)got, (unsigned long long)reference);
        }
        TEST_ASSERT(got == reference);
    }
    TEST_ASSERT(simd_popcount(buf, sizeof(buf)) == reference);
}

static void test_bitops_all(void) {
    struct backend_list bl = get_all_backends();

    uint8_t a[129], b[129], out[129];
    fill_random_u8(a, sizeof(a));
    fill_random_u8(b, sizeof(b));

    for (int i = 0; i < bl.count; i++) {
        memset(out, 0xCC, sizeof(out));
        bl.b[i]->fn_bit_and(out, a, b, sizeof(a));
        for (size_t k = 0; k < sizeof(a); k++) {
            TEST_ASSERT(out[k] == (uint8_t)(a[k] & b[k]));
        }
    }

    for (int i = 0; i < bl.count; i++) {
        bl.b[i]->fn_bit_or(out, a, b, sizeof(a));
        for (size_t k = 0; k < sizeof(a); k++) {
            TEST_ASSERT(out[k] == (uint8_t)(a[k] | b[k]));
        }
    }

    for (int i = 0; i < bl.count; i++) {
        bl.b[i]->fn_bit_xor(out, a, b, sizeof(a));
        for (size_t k = 0; k < sizeof(a); k++) {
            TEST_ASSERT(out[k] == (uint8_t)(a[k] ^ b[k]));
        }
    }

    for (int i = 0; i < bl.count; i++) {
        bl.b[i]->fn_bit_andnot(out, a, b, sizeof(a));
        for (size_t k = 0; k < sizeof(a); k++) {
            TEST_ASSERT(out[k] == (uint8_t)(a[k] & ~b[k]));
        }
    }
}

/* ====================================================================
 * TEST: hash
 * ==================================================================== */

static void test_hash_all(void) {
    struct backend_list bl = get_all_backends();

    uint64_t ref = bl.b[0]->fn_hash64(0x123456789ABCDEF0ULL);
    for (int i = 1; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_hash64(0x123456789ABCDEF0ULL) == ref);
    }
    TEST_ASSERT(simd_hash64(0x123456789ABCDEF0ULL) == ref);

    uint64_t buf[16];
    fill_random_u64(buf, 16);

    uint64_t refbuf = bl.b[0]->fn_hash64_buf(buf, 16);
    for (int i = 1; i < bl.count; i++) {
        TEST_ASSERT(bl.b[i]->fn_hash64_buf(buf, 16) == refbuf);
    }
    TEST_ASSERT(simd_hash64_buf(buf, 16) == refbuf);

    uint64_t swapped[16];
    memcpy(swapped, buf, sizeof(swapped));
    uint64_t t = swapped[0]; swapped[0] = swapped[1]; swapped[1] = t;
    TEST_ASSERT(simd_hash64_buf(swapped, 16) != refbuf);
}

/* ====================================================================
 * TEST: edge case sizes
 * ==================================================================== */

static void test_edge_sizes(void) {
    struct backend_list bl = get_all_backends();

    const size_t sizes[] = { 0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128, 129 };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int s = 0; s < n; s++) {
        size_t len = sizes[s];
        uint64_t buf[16] = { 0 };
        for (size_t i = 0; i < 16 && i * 8 < len; i++) {
            buf[i] = 0xDEADBEEFCAFEBABEULL;
        }

        size_t count = (len + 7) / 8;
        if (count > 16) count = 16;

        uint64_t ref = bl.b[0]->fn_find_u64(buf, 0xDEADBEEFCAFEBABEULL, count);
        for (int i = 1; i < bl.count; i++) {
            TEST_ASSERT(bl.b[i]->fn_find_u64(buf, 0xDEADBEEFCAFEBABEULL, count) == ref);
        }
    }
}

/* ====================================================================
 * Main
 * ==================================================================== */

int main(void) {
#ifdef _WIN32
    printf("== test_simd on Windows ==\n");
#elif defined(__APPLE__)
    printf("== test_simd on macOS ==\n");
#elif defined(__linux__)
    printf("== test_simd on Linux ==\n");
#else
    printf("== test_simd on unknown platform ==\n");
#endif

#if defined(__x86_64__) || defined(_M_X64)
    printf("   arch:     x86_64\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
    printf("   arch:     aarch64\n");
#endif

    printf("   active:   %s\n", simd_isa_name(simd_active_isa()));
    fflush(stdout);

    TEST_RUN(test_init);
    TEST_RUN(test_memcpy_all);
    TEST_RUN(test_memset_all);
    TEST_RUN(test_memcmp_all);
    TEST_RUN(test_find_u64_all);
    TEST_RUN(test_find_u32_all);
    TEST_RUN(test_find_u16_all);
    TEST_RUN(test_find_u8_all);
    TEST_RUN(test_cmp_all);
    TEST_RUN(test_popcount_all);
    TEST_RUN(test_bitops_all);
    TEST_RUN(test_hash_all);
    TEST_RUN(test_edge_sizes);

    TEST_SUMMARY();
}