/**
 * @file _m_mem.h
 * @brief Aligned memory allocation and platform info queries.
 *
 * The database needs aligned memory for a few reasons:
 *
 *   - SIMD loads often require 16- or 32-byte alignment.
 *   - Direct I/O (O_DIRECT on Linux, FILE_FLAG_NO_BUFFERING on Windows)
 *     requires buffer alignment to the device's sector size, typically
 *     512 or 4096.
 *   - Cache-line-friendly struct layouts want 64-byte alignment.
 *
 * These functions wrap posix_memalign on POSIX and _aligned_malloc on
 * Windows. Both must be paired with the matching free: posix_memalign
 * uses free(), _aligned_malloc uses _aligned_free(). _m_free handles the
 * dispatch, so callers always use _m_free.
 *
 * PAGE SIZE vs ALLOCATION GRANULARITY
 * -----------------------------------
 * These are two different things and both are needed:
 *
 *   Page size: the unit of virtual memory. mmap offset alignment on POSIX.
 *              4 KiB on x86 Linux, 16 KiB on Apple Silicon, 4 KiB on Windows.
 *
 *   Allocation granularity: the minimum alignment for the BASE ADDRESS of
 *              an mmap on Windows. 64 KiB, always. On POSIX it equals the
 *              page size.
 *
 * For a portable database, preallocating in 64 KiB clusters makes every
 * mapping offset naturally aligned on every platform.
 */

#ifndef M__M_MEM_H
#define M__M_MEM_H

#include "m/_m_types.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Allocate aligned memory.
	 *
	 * @param alignment  Required alignment in bytes. Must be a power of two
	 *                   and a multiple of sizeof(void*). 16, 32, 64 are common.
	 * @param size       Number of bytes. Must be > 0.
	 *
	 * @return _m_ptr_result_t where on success ptr is the allocated block.
	 *         Free with _m_free, NOT free().
	 *
	 * @note On POSIX, calls posix_memalign. On Windows, _aligned_malloc.
	 *       The returned pointer on success is guaranteed to be a multiple
	 *       of `alignment`.
	 */
	_m_ptr_result_t _m_alloc(_m_size_t alignment, _m_size_t size);

	/**
	 * @brief Free memory returned by _m_alloc.
	 *
	 * @param ptr  Pointer returned by _m_alloc. NULL is accepted (no-op).
	 *
	 * @note Must NOT be used to free memory from malloc, calloc, or any other
	 *       allocator. On Windows the underlying call is _aligned_free, which
	 *       is not compatible with free.
	 */
	void _m_free(void* ptr);

	/**
	 * @brief Get the system's virtual memory page size.
	 *
	 * @return Page size in bytes. Never 0.
	 *
	 * @note On Linux this is typically 4096; on Apple Silicon, 16384;
	 *       on Windows, 4096. Callers should not assume any specific value.
	 */
	_m_size_t _m_page_size(void);

	/**
	 * @brief Get the minimum alignment for the base address of an mmap.
	 *
	 * @return Granularity in bytes. Never 0.
	 *
	 * @note On Windows, always 65536 (64 KiB). On POSIX, equal to page size.
	 *
	 * @note A database that preallocates files in multiples of 64 KiB can
	 *       map any cluster boundary on either platform without per-platform
	 *       alignment logic. This is the primary reason to choose 64 KiB as
	 *       your cluster size.
	 */
	_m_size_t _m_alloc_granularity(void);

#ifdef __cplusplus
}
#endif

#endif /* M__M_MEM_H */