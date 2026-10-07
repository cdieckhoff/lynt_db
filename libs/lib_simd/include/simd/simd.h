/**
 * @file simd.h
 * @brief Portable SIMD abstraction for lynt_db.
 *
 * WHAT THIS PROVIDES
 * ------------------
 * A single API for the vector operations a storage engine needs —
 * memory primitives, search-and-filter primitives, bitmap operations,
 * and mask helpers — with runtime dispatch to the best available ISA
 * on the host CPU.
 *
 * The caller never chooses an ISA. The library detects capabilities at
 * init time and routes every call to the best implementation available:
 *   - x86-64 with AVX2      → AVX2 implementation
 *   - x86-64 without AVX2   → SSE4.2 implementation
 *   - ARM64 (Apple Silicon, Windows on ARM, Linux ARM) → NEON
 *   - Anything else         → scalar fallback
 *
 * The public API is IDENTICAL on all platforms and all ISA levels.
 *
 * ALIGNMENT
 * ---------
 * No function in this library REQUIRES aligned input. Every function
 * works with unaligned buffers. When input happens to be aligned to
 * 16 or 32 bytes, the implementations take a faster path, but
 * correctness is never conditional on alignment.
 *
 * PORTABILITY
 * -----------
 * This header includes only the standard C library. No compiler
 * intrinsics, no <intrin.h>, no MSVC-specific branches. The mask
 * helpers use portable bit-twiddling that compiles on every C11
 * compiler for every architecture. The cost versus hardware
 * instructions is a few cycles per call, and these functions are
 * only used to iterate set bits after the SIMD work is done, so the
 * difference is unmeasurable in practice.
 */

#ifndef SIMD_H
#define SIMD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* ====================================================================
     * SECTION: Initialization and introspection
     * ==================================================================== */

     /**
      * @brief Which SIMD implementation is active on this host.
      *
      * Returned by simd_active_isa() for diagnostics and logging. The
      * caller never needs to branch on this; it's informational.
      */
    typedef enum {
        SIMD_ISA_SCALAR = 0,   /**< Portable C, no vectorization */
        SIMD_ISA_SSE42 = 1,   /**< x86-64 baseline (2008+) */
        SIMD_ISA_AVX2 = 2,   /**< x86-64 with AVX2 + FMA (2013+) */
        SIMD_ISA_NEON = 3,   /**< ARMv8-A 64-bit NEON */
    } simd_isa_t;

    /**
     * @brief Initialize the SIMD dispatch table.
     *
     * Called automatically on first use of any simd_* function. Calling
     * explicitly is optional and idempotent. Thread-safe.
     *
     * @return The ISA that was selected.
     */
    simd_isa_t simd_init(void);

    /**
     * @brief Query the currently active ISA.
     *
     * @return The active ISA. If simd_init has not been called, calling
     *         this function triggers initialization.
     */
    simd_isa_t simd_active_isa(void);

    /**
     * @brief Human-readable name for an ISA.
     *
     * @return A static string, never NULL. E.g. "scalar", "sse4.2",
     *         "avx2", "neon".
     */
    const char* simd_isa_name(simd_isa_t isa);

    /* ====================================================================
     * SECTION: Memory primitives
     * ==================================================================== */

     /**
      * @brief Copy a block of memory.
      *
      * Same contract as memcpy: dst and src must not overlap.
      */
    void simd_memcpy(void* dst, const void* src, size_t n);

    /**
     * @brief Copy a block of memory, handling overlap.
     *
     * Same contract as memmove.
     */
    void simd_memmove(void* dst, const void* src, size_t n);

    /**
     * @brief Fill memory with a byte value.
     *
     * Same contract as memset.
     */
    void simd_memset(void* dst, int value, size_t n);

    /**
     * @brief Compare two memory blocks lexicographically.
     *
     * Same contract as memcmp: returns <0, 0, or >0.
     *
     * The SIMD implementations compare in wide chunks and only fall back
     * to byte-by-byte comparison if a difference is found. This makes the
     * common case (equal blocks) much faster than a naive scalar loop.
     */
    int simd_memcmp(const void* a, const void* b, size_t n);

    /**
     * @brief Find the first occurrence of a byte in a buffer.
     *
     * Same contract as memchr: returns a pointer to the first byte equal
     * to `c`, or NULL if not found.
     */
    void* simd_memchr(const void* buf, int c, size_t n);

    /* ====================================================================
     * SECTION: Search primitives (bitmask-returning)
     *
     * These are the workhorses for scanning pages of records. Each returns
     * a bitmask with bit i set if the value at index i matches the target.
     * The mask has exactly `count` significant bits.
     *
     * The reason these return masks rather than indices or booleans is
     * branch elimination. A scalar loop with `if (buf[i] == target)` has
     * one branch per iteration, and branch mispredictions dominate on
     * unpredictable data. A SIMD implementation computes the mask with
     * zero branches and lets the caller decide how to interpret the result.
     *
     * Callers who want "find the first match" do:
     *     uint64_t m = simd_find_u64(buf, target, count);
     *     if (m) { size_t idx = simd_mask_first(m); ... }
     *
     * Callers who want "iterate all matches" do:
     *     uint64_t m = simd_find_u64(buf, target, count);
     *     while (m) {
     *         size_t idx = simd_mask_first(m);
     *         ...
     *         m &= m - 1;   // clear lowest set bit
     *     }
     * ==================================================================== */

     /**
      * @brief Find all occurrences of a 64-bit value in a u64 buffer.
      *
      * @param buf    Array of 64-bit values.
      * @param target Value to search for.
      * @param count  Number of elements in buf.
      * @return A 64-bit mask. Bit i is set iff buf[i] == target.
      *
      * @note Only the low `count` bits are meaningful. If count > 64, use
      *       multiple calls with an offset.
      */
    uint64_t simd_find_u64(const uint64_t* buf, uint64_t target, size_t count);

    /**
     * @brief Find all occurrences of a 32-bit value in a u32 buffer.
     */
    uint64_t simd_find_u32(const uint32_t* buf, uint32_t target, size_t count);

    /**
     * @brief Find all occurrences of a 16-bit value in a u16 buffer.
     */
    uint64_t simd_find_u16(const uint16_t* buf, uint16_t target, size_t count);

    /**
     * @brief Find all occurrences of a byte in a byte buffer.
     */
    uint64_t simd_find_u8(const uint8_t* buf, uint8_t target, size_t count);

    /* ====================================================================
     * SECTION: Comparison primitives
     * ==================================================================== */

     /**
      * @brief Compare two u64 buffers element-wise.
      *
      * @return A 64-bit mask. Bit i is set iff a[i] == b[i].
      */
    uint64_t simd_cmp_u64(const uint64_t* a, const uint64_t* b, size_t count);

    /**
     * @brief Compare two u32 buffers element-wise.
     */
    uint64_t simd_cmp_u32(const uint32_t* a, const uint32_t* b, size_t count);

    /**
     * @brief Compare two byte buffers element-wise.
     */
    uint64_t simd_cmp_u8(const uint8_t* a, const uint8_t* b, size_t count);

    /* ====================================================================
     * SECTION: Bit operations
     * ==================================================================== */

     /**
      * @brief Count the number of set bits in a byte buffer.
      *
      * Used for bitmaps, bloom filters, and sparse-extent summaries.
      */
    uint64_t simd_popcount(const void* buf, size_t n);

    /**
     * @brief Bitwise AND of two byte buffers, storing the result in dst.
     *
     * dst, a, and b may alias. All buffers must be at least n bytes.
     */
    void simd_bit_and(void* dst, const void* a, const void* b, size_t n);

    /**
     * @brief Bitwise OR of two byte buffers.
     */
    void simd_bit_or(void* dst, const void* a, const void* b, size_t n);

    /**
     * @brief Bitwise XOR of two byte buffers.
     */
    void simd_bit_xor(void* dst, const void* a, const void* b, size_t n);

    /**
     * @brief Bitwise AND-NOT: dst = a & ~b.
     */
    void simd_bit_andnot(void* dst, const void* a, const void* b, size_t n);

    /* ====================================================================
     * SECTION: Mask helpers
     *
     * These are the operations a caller reaches for after a search or
     * comparison function returns a bitmask. They are declared static
     * inline so there's no per-call function pointer indirection and no
     * linkage complexity — the compiler inlines them at the call site.
     *
     * They do NOT dispatch through the ISA table. A ctz or popcount on a
     * 64-bit scalar doesn't benefit from SIMD, and using the hardware
     * instructions would require a compiler intrinsic or a header, which
     * is exactly what we're avoiding to keep the header portable.
     *
     * The cost of the portable versions is a few cycles per call versus
     * the hardware instruction. Since these are called once per match
     * found during mask iteration — and matches are rare in the inner
     * loops of a database — the difference is not measurable.
     * ==================================================================== */

     /**
      * @brief Return the index of the lowest set bit in a mask.
      *
      * @param mask A 64-bit mask. Must be nonzero.
      * @return Index of the lowest set bit (0–63).
      *
      * @note Behavior is undefined if mask is 0. Callers should test
      *       `if (mask)` first, or use simd_mask_first_or(mask, fallback).
      *
      * Portable implementation using a 64-entry De Bruijn sequence. The
      * technique: isolate the lowest set bit with (mask & -mask), multiply
      * by a magic constant so the isolated bit's position maps to a unique
      * 6-bit index in the high bits of the product, then use that index to
      * look up the position in a table. Compiles to a negate, an AND, an
      * imul, a shift, and a table load — about 5 cycles on modern CPUs,
      * versus 3 for the hardware TZCNT instruction.
      */
    static inline size_t simd_mask_first(uint64_t mask) {
        static const uint8_t debruijn64[64] = {
             0,  1,  2, 53,  3,  7, 54, 27,
             4, 38, 41,  8, 34, 55, 48, 28,
            62,  5, 39, 46, 44, 42, 22,  9,
            24, 35, 59, 56, 49, 18, 29, 11,
            63, 52,  6, 26, 37, 40, 33, 47,
            61, 45, 43, 21, 23, 58, 17, 10,
            51, 25, 36, 32, 60, 20, 57, 16,
            50, 31, 19, 15, 30, 14, 13, 12
        };
        uint64_t lowest = mask & (~mask + 1);   /* isolate lowest set bit */
        return (size_t)debruijn64[(lowest * 0x022FDD63CC95386DULL) >> 58];
    }

    /**
     * @brief Return the index of the lowest set bit, or a fallback.
     *
     * Convenience wrapper for callers that would otherwise write
     * `mask ? simd_mask_first(mask) : fallback`. The fallback is
     * typically SIZE_MAX to signal "no match".
     */
    static inline size_t simd_mask_first_or(uint64_t mask, size_t fallback) {
        return mask ? simd_mask_first(mask) : fallback;
    }

    /**
     * @brief Popcount of a 64-bit mask.
     *
     * Portable SWAR (SIMD Within A Register) popcount. The technique
     * treats the 64-bit value as 64 one-bit counters and folds them
     * together in a sequence of shift-and-add steps, halving the number
     * of counters each round. Compiles to about a dozen ALU operations —
     * roughly 12 cycles, versus 3 for the hardware POPCNT instruction.
     *
     * Same rationale as simd_mask_first: called once per match, and
     * matches are rare in hot loops, so the difference isn't measurable.
     */
    static inline int simd_mask_count(uint64_t mask) {
        mask = mask - ((mask >> 1) & 0x5555555555555555ULL);
        mask = (mask & 0x3333333333333333ULL)
            + ((mask >> 2) & 0x3333333333333333ULL);
        mask = (mask + (mask >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
        return (int)((mask * 0x0101010101010101ULL) >> 56);
    }

    /* ====================================================================
     * SECTION: Hashing
     * ==================================================================== */

     /**
      * @brief Mix a 64-bit value into a well-distributed 64-bit hash.
      *
      * This is a finalizer, not a full hash function. Feed it the output of
      * your hash-update function to get a well-distributed hash. Suitable
      * for hash-table indexing, bloom filter bits, and index bucket
      * selection.
      *
      * Deterministic and stable across platforms and versions — the same
      * input always produces the same output. This is important for
      * persistent data structures.
      *
      * Uses the splitmix64 finalizer. Every backend produces identical
      * results.
      */
    uint64_t simd_hash64(uint64_t x);

    /**
     * @brief Hash a buffer of 64-bit values.
     *
     * Feeds each 64-bit word through simd_hash64 in sequence. The result
     * is order-sensitive and deterministic.
     */
    uint64_t simd_hash64_buf(const uint64_t* buf, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* SIMD_H */