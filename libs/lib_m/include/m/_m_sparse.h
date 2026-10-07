/**
 * @file _m_sparse.h
 * @brief Sparse file operations: hole punching, detection, and query.
 *
 * A sparse file has regions that read as zeros but occupy no disk blocks.
 * For a database that preallocates large regions via _m_truncate, sparse
 * files mean you can promise the OS a 1 TiB file while only consuming
 * disk space for the pages you actually write.
 *
 * PLATFORM SUPPORT
 * ----------------
 * Linux:   fallocate(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE) to punch.
 *          lseek(fd, off, SEEK_DATA/SEEK_HOLE) to query.
 *          Requires ext4, xfs, btrfs, or tmpfs.
 *
 * macOS:   fcntl(fd, F_PUNCHHOLE) to punch. APFS and HFS+ (10.12+).
 *          fcntl(fd, F_LOG2PHYS) to query (best effort).
 *
 * Windows: FSCTL_SET_SPARSE to mark the file sparse (NTFS only).
 *          FSCTL_SET_ZERO_DATA to punch.
 *          FSCTL_QUERY_ALLOCATED_RANGES to query.
 *
 * NOT ALL FILESYSTEMS SUPPORT SPARSE FILES. Use _m_mark_sparse and check
 * its return; if it fails, the file will still work but punching holes
 * will allocate zero blocks instead of deallocating them.
 *
 * ALIGNMENT
 * ---------
 * Hole punching is aligned to the filesystem block size. On Linux this is
 * typically 4096; on Windows, the cluster size (typically 4096 for NTFS).
 * If you pass an unaligned offset or length, the punch either fails or is
 * expanded to block boundaries. The _m layer does NOT silently align for
 * you; it returns _M_ERR_INVALID_ARG if you call _m_punch_hole with
 * unaligned arguments. The caller is expected to know the alignment, which
 * it can query via _m_alloc_granularity() in _m_mem.h.
 */

#ifndef M__M_SPARSE_H
#define M__M_SPARSE_H

#include "m/_m_types.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Mark a file as sparse.
	 *
	 * @param h  An open handle.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note On Linux and macOS this is a no-op that always succeeds; sparse
	 *       files are created implicitly by writing past EOF or by
	 *       ftruncate'ing to a larger size.
	 *
	 * @note On Windows, this sends FSCTL_SET_SPARSE. It must be called before
	 *       FSCTL_SET_ZERO_DATA will actually deallocate blocks; without it,
	 *       punching a hole just writes zeros and consumes disk space.
	 *
	 * @note Returns _M_ERR_NOT_SUPPORTED if the filesystem does not support
	 *       sparse files (FAT, exFAT, some network filesystems). Callers
	 *       should treat this as a soft failure and continue without sparse
	 *       semantics.
	 */
	_m_result_t _m_mark_sparse(_m_handle_t h);

	/**
	 * @brief Deallocate a region of a file.
	 *
	 * The region reads as zeros after this call, and the disk blocks that
	 * previously backed it are freed. File size is unchanged.
	 *
	 * @param h       An open handle.
	 * @param offset  Start offset. Must be aligned to the filesystem's hole
	 *                granularity (typically 4096; check _m_alloc_granularity).
	 * @param length  Length in bytes. Must be aligned. Must be > 0.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note If the region extends past EOF, the behavior is OS-defined. On
	 *       Linux it is a no-op past EOF; on Windows it returns an error.
	 *       Callers should keep the range within the file.
	 *
	 * Error codes:
	 *   _M_ERR_INVALID_ARG     unaligned offset/length, or length == 0
	 *   _M_ERR_NOT_SUPPORTED   filesystem does not support hole punching
	 *   _M_ERR_IO              OS-level failure
	 */
	_m_result_t _m_punch_hole(_m_handle_t h, _m_size_t offset, _m_size_t length);

	/**
	 * @brief Query whether a region of a file is a hole (unallocated).
	 *
	 * @param h        An open handle.
	 * @param offset   Start offset.
	 * @param length   Length to check. Must be > 0.
	 * @param out      Output: set to true if the entire region is a hole.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note "Entire region is a hole" is strict: if any part of the range is
	 *       backed by allocated storage, `*out` is set to false.
	 *
	 * @note On filesystems without hole-detection support, this returns
	 *       _M_ERR_NOT_SUPPORTED. Callers should treat that as "unknown",
	 *       not as "false".
	 */
	_m_result_t _m_is_hole(_m_handle_t h, _m_size_t offset, _m_size_t length, bool* out);

	/**
	 * @brief Query the number of bytes actually allocated on disk.
	 *
	 * @param h  An open handle.
	 *
	 * @return _m_result_t where value is the allocated byte count.
	 *
	 * @note On POSIX, this reads st_blocks * 512 from fstat. This reflects
	 *       the true on-disk consumption, which will be much smaller than
	 *       the logical size for a mostly-empty sparse file.
	 *
	 * @note On Windows, there is no equivalent one-call query; the closest is
	 *       FSCTL_GET_RETRIEVAL_POINTERS, which is complex. This function
	 *       currently returns the LOGICAL size on Windows. If you need true
	 *       allocation accounting on Windows, that's a follow-up.
	 */
	_m_result_t _m_allocated_size(_m_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* M__M_SPARSE_H */