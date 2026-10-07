/**
 * @file _m_file.h
 * @brief File lifecycle operations: open, close, truncate, size, sync.
 *
 * These wrap the POSIX / Win32 equivalents of:
 *   open(2), close(2), ftruncate(2), fstat(2) (for size), fsync(2).
 *
 * On Windows, _m_open uses CreateFileW (wide char) so that UTF-8 paths with
 * non-ASCII characters work correctly. The public API takes UTF-8 char*;
 * the conversion to UTF-16 happens inside _m_open.
 */

#ifndef M__M_FILE_H
#define M__M_FILE_H

#include "m/_m_types.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Open or create a file.
	 *
	 * @param path    NUL-terminated UTF-8 path. Must not be NULL.
	 * @param flags   Bitwise OR of _M_O_* values.
	 * @param mode    POSIX permission bits (e.g. 0644). Ignored on Windows.
	 * @param sparse  If true, mark the file as sparse at open time.
	 *                - On Linux and macOS, this is a no-op; sparse files
	 *                  are created implicitly by writing past EOF or by
	 *                  ftruncate'ing to a larger size.
	 *                - On Windows, this passes FILE_ATTRIBUTE_SPARSE_FILE
	 *                  to CreateFileW. If the filesystem does not support
	 *                  sparse files (FAT, exFAT, some network filesystems),
	 *                  the flag is ignored and the file opens normally.
	 *                  This matches POSIX behavior where sparse is best-
	 *                  effort, not a hard requirement.
	 *
	 * @return _m_result_t where on success value holds an _m_handle_t.
	 *
	 * @note The `sparse` flag is the ONLY platform-specific behavior in this
	 *       function that callers need to know about. Every other aspect
	 *       behaves identically on all three platforms.
	 */
	_m_result_t _m_open(const char* path, int flags, int mode, bool sparse);

	/**
	 * @brief Close a file handle.
	 *
	 * @param h  A handle previously returned by _m_open.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note Passing _M_HANDLE_INVALID is an error (_M_ERR_INVALID_ARG).
	 * @note Closing a handle twice is undefined behavior. Don't.
	 */
	_m_result_t _m_close(_m_handle_t h);

	/**
	 * @brief Resize a file to the given length.
	 *
	 * @param h     An open handle.
	 * @param size  New length in bytes. May be larger or smaller than current.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note On POSIX, this calls ftruncate(2). On Linux and macOS, growing a
	 *       file this way creates a hole (sparse region) rather than allocating
	 *       real disk blocks. Reading the hole returns zeros. This is exactly
	 *       what a database wants for preallocated-but-unused pages.
	 *
	 * @note On Windows, we SetFilePointerEx to `size` then call SetEndOfFile.
	 *       Growing a sparse-marked file also creates a hole. See _m_sparse.h
	 *       for _m_mark_sparse().
	 *
	 * @note The file position is not affected on POSIX. On Windows it moves
	 *       to `size`; callers who care should re-seek afterward.
	 */
	_m_result_t _m_truncate(_m_handle_t h, _m_size_t size);

	/**
	 * @brief Query the current logical size of a file.
	 *
	 * @param h  An open handle.
	 *
	 * @return _m_result_t where value is the size in bytes.
	 *
	 * @note This is the LOGICAL size (the value reported by stat / GetFileSizeEx).
	 *       It is NOT the same as the number of bytes actually allocated on disk.
	 *       For the latter, use _m_allocated_size() in _m_sparse.h.
	 *
	 * @note The size is cached nowhere in _m; each call goes to the OS. If you
	 *       need to call this in a hot loop, cache the result in your caller.
	 */
	_m_result_t _m_size(_m_handle_t h);

	/**
	 * @brief Flush pending writes and metadata to stable storage.
	 *
	 * @param h  An open handle.
	 *
	 * @return _m_result_t where value is 0 on success.
	 *
	 * @note On POSIX this calls fsync(2). On Windows, FlushFileBuffers.
	 * @note This is a durability barrier, not a cache flush. It forces the OS
	 *       to commit the file's data and metadata to the storage device.
	 * @note For flushing a memory-mapped region, use _m_msync() instead.
	 */
	_m_result_t _m_sync(_m_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* M__M_FILE_H */