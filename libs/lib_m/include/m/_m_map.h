/**
 * @file _m_map.h
 * @brief Memory-mapped file I/O with POSIX semantics on all platforms.
 *
 * PHILOSOPHY
 * ----------
 * The _m layer presents POSIX mmap semantics on every platform. On
 * Windows, this means hiding the 64 KiB allocation granularity so that
 * callers can map at any file offset, including 16 KiB boundaries.
 *
 * THE WINDOWS GRANULARITY PROBLEM
 * -------------------------------
 * Windows' MapViewOfFileEx requires the file offset to be a multiple of
 * dwAllocationGranularity (always 65536). POSIX mmap requires only page
 * alignment (typically 4096). A database that uses 16 KiB pages cannot
 * map at offset 16384 on Windows without help.
 *
 * The solution is to map at the nearest lower granularity boundary
 * and return a pointer offset into the mapping:
 *
 *     Caller wants:   offset = 16384, length = 16384
 *     Windows sees:   aligned_offset = 0
 *                     intra_offset   = 16384
 *                     aligned_length = 16384 + 16384 = 32768
 *     Windows maps:   MapViewOfFileEx(..., offset=0, length=32768)
 *     Library returns: base + 16384
 *
 * The caller's pointer (base + 16384) is NOT the base address that
 * UnmapViewOfFile needs. To handle this cleanly, _m_map returns an
 * opaque _m_mapping_t* instead of a raw void*. Callers extract the
 * usable pointer with _m_mapping_addr() and release the mapping with
 * _m_unmap().
 *
 * On Linux and macOS, the alignment is already exact: intra_offset is
 * always 0, aligned_offset equals offset, and the returned pointer is
 * the same as the base. The _m_mapping_t still exists for API
 * uniformity; it just holds simpler data.
 *
 * EXTENDING PAST EOF
 * ------------------
 * POSIX mmap permits mapping past EOF. Reads in the mapped region
 * beyond EOF return zeros; writes extend the file. Windows
 * CreateFileMapping refuses to map past EOF unless the file is large
 * enough. To match POSIX, _m_map extends the file to cover the mapping
 * before calling CreateFileMapping. This is a small amount of extra
 * work on Windows and a no-op on POSIX.
 */

#ifndef M__M_MAP_H
#define M__M_MAP_H

#include "m/_m_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Concrete definition of an _m_mapping_t.
     *
     * Callers must not inspect these fields directly. Use _m_mapping_addr()
     * to get the usable pointer. The struct is declared here only because
     * the compiler needs to know its size for stack allocation in some
     * call sites; opaque-pointer usage is otherwise expected.
     */
    struct _m_mapping {
        /**
         * The address returned by the OS mapping call.
         *   POSIX:   the value returned by mmap.
         *   Windows: the value returned by MapViewOfFileEx at aligned_offset.
         * This is what gets passed to munmap / UnmapViewOfFile.
         */
        void* base;

        /**
         * The length passed to the OS mapping call.
         *   POSIX:   == caller's len.
         *   Windows: intra_offset + caller's len, rounded up to page size.
         * This is what gets passed to munmap on POSIX. UnmapViewOfFile
         * ignores it, but we keep it for symmetry and diagnostics.
         */
        _m_size_t aligned_len;

        /**
         * The pointer the caller should use.
         *   POSIX:   == base (no adjustment).
         *   Windows: base + intra_offset.
         */
        void* user_ptr;

        /**
         * File offset that was passed to the OS mapping call.
         *   POSIX:   == caller's offset.
         *   Windows: granularity-aligned offset.
         */
        _m_size_t aligned_off;
    };

    /**
     * @brief Map a region of a file into memory.
     *
     * Presents POSIX semantics on every platform. The caller passes the
     * file offset they want to access. On Windows, if that offset is not
     * granularity-aligned, the library maps at the nearest lower boundary
     * and returns a pointer shifted into the mapping. The caller never
     * sees the difference.
     *
     * @param addr    Hint address for the mapping, or NULL for OS-chosen.
     * @param len     Length in bytes. Must be > 0.
     * @param prot    Bitwise OR of _M_PROT_READ and _M_PROT_WRITE.
     * @param flags   _M_MAP_SHARED or _M_MAP_PRIVATE.
     * @param h       An open handle.
     * @param offset  File offset. Must be >= 0. Does NOT need to be aligned
     *                to anything; the library handles alignment internally.
     *
     * @return _m_ptr_result_t where on success ptr is an _m_mapping_t*.
     *         Use _m_mapping_addr() to extract the caller-usable pointer.
     *         Release with _m_unmap().
     *
     * @note If offset + len exceeds the current file size, the file is
     *       extended to offset + len. This matches POSIX behavior where
     *       mapping past EOF is permitted and reads return zeros.
     *
     * @note The mapping is shared (_M_MAP_SHARED) or private (_M_MAP_PRIVATE)
     *       depending on `flags`. Databases almost always want shared.
     */
    _m_ptr_result_t _m_map(void* addr, _m_size_t len, int prot, int flags,
        _m_handle_t h, _m_size_t offset);

    /**
     * @brief Extract the caller-usable pointer from a mapping.
     *
     * @param m  The _m_mapping_t* returned by _m_map.
     *
     * @return A pointer to the first byte of the requested region. Valid
     *         for exactly `len` bytes as passed to _m_map. Never NULL for
     *         a mapping that _m_map returned successfully.
     *
     * @note This is the pointer to use for reads and writes. Do NOT use
     *       `m->base` directly; on Windows, base is the granularity-aligned
     *       address and may point to bytes before the caller's region.
     */
    void* _m_mapping_addr(_m_mapping_t* m);

    /**
     * @brief Release a mapping.
     *
     * @param m  The _m_mapping_t* returned by _m_map.
     *
     * @return _m_result_t where value is 0 on success.
     *
     * @note On Windows, this calls UnmapViewOfFile with the base pointer
     *       captured at _m_map time, not the caller's user pointer.
     *       Callers must not attempt to compute the base themselves.
     *
     * @note After this call, the _m_mapping_t* and any pointer returned by
     *       _m_mapping_addr() are invalid. Do not use them.
     */
    _m_result_t _m_unmap(_m_mapping_t* m);

    /**
     * @brief Flush dirty pages of a mapping to disk.
     *
     * @param m  The _m_mapping_t* returned by _m_map.
     *
     * @return _m_result_t where value is 0 on success.
     *
     * @note POSIX msync with MS_SYNC blocks until the pages are on disk.
     *       Windows FlushViewOfFile is asynchronous. For a true durability
     *       barrier on Windows, follow with _m_sync(h) on the underlying
     *       handle.
     *
     * @note The entire mapping is flushed. There is no partial-flush API
     *       because the common case is "flush everything" at a checkpoint.
     */
    _m_result_t _m_msync(_m_mapping_t* m);

#ifdef __cplusplus
}
#endif

#endif /* M__M_MAP_H */