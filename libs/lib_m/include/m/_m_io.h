/**
 * @file _m_io.h
 * @brief Sequential and positional read/write operations.
 *
 * Four functions:
 *   _m_read   / _m_write    use and advance the file position
 *   _m_pread  / _m_pwrite   operate at an explicit offset, leaving the
 *                           file position untouched
 *
 * For a database, pread/pwrite are the important ones. They are safe to
 * call concurrently on the same handle from multiple threads (each call
 * carries its own offset), whereas read/write share a single file position
 * and therefore require external synchronization.
 *
 * On Windows, pread/pwrite are implemented with ReadFile/WriteFile plus an
 * OVERLAPPED struct carrying the offset. Contrary to a common misconception,
 * the file does NOT need to be opened with FILE_FLAG_OVERLAPPED for this to
 * work; passing an OVERLAPPED to ReadFile on a synchronous handle performs
 * a blocking read at the given offset. That is exactly what we want.
 */

#ifndef M__M_IO_H
#define M__M_IO_H

#include "m/_m_types.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Read up to `n` bytes at the current file position.
	 *
	 * @param h    An open handle, opened for reading.
	 * @param buf  Destination buffer. Must be non-NULL if n > 0.
	 * @param n    Maximum bytes to read.
	 *
	 * @return _m_result_t where value is the number of bytes actually read.
	 *         A short read (value < n) is not an error; it means EOF was
	 *         reached or the OS returned fewer bytes than requested.
	 *         value == 0 means EOF was already at the current position.
	 *
	 * @note Advances the file position by the number of bytes read.
	 * @note Not thread-safe with respect to other read/write calls on the same
	 *       handle. Use _m_pread for concurrent access.
	 *
	 * Error codes:
	 *   _M_ERR_INVALID_ARG  h invalid, or buf NULL with n > 0
	 *   _M_ERR_IO           the OS read failed
	 */
	_m_result_t _m_read(_m_handle_t h, void* buf, _m_size_t n);

	/**
	 * @brief Write up to `n` bytes at the current file position.
	 *
	 * @param h    An open handle, opened for writing.
	 * @param buf  Source buffer. Must be non-NULL if n > 0.
	 * @param n    Bytes to write.
	 *
	 * @return _m_result_t where value is the number of bytes actually written.
	 *         A short write is not an error per se, but callers who need
	 *         exactly `n` bytes should loop.
	 *
	 * @note Advances the file position by the number of bytes written.
	 * @note Not thread-safe with respect to other read/write calls on the same
	 *       handle. Use _m_pwrite for concurrent access.
	 */
	_m_result_t _m_write(_m_handle_t h, const void* buf, _m_size_t n);

	/**
	 * @brief Read up to `n` bytes at an explicit offset.
	 *
	 * @param h       An open handle, opened for reading.
	 * @param buf     Destination buffer.
	 * @param n       Maximum bytes to read.
	 * @param offset  File offset to read from. Must be >= 0.
	 *
	 * @return _m_result_t where value is the number of bytes read.
	 *
	 * @note Does NOT modify the file position. Safe to call concurrently on the
	 *       same handle from multiple threads. This is the primitive a
	 *       memory-mapped database should use for all non-mapped I/O.
	 *
	 * @note If `offset` is beyond EOF, value is 0 (not an error).
	 */
	_m_result_t _m_pread(_m_handle_t h, void* buf, _m_size_t n, _m_size_t offset);

	/**
	 * @brief Write up to `n` bytes at an explicit offset.
	 *
	 * @param h       An open handle, opened for writing.
	 * @param buf     Source buffer.
	 * @param n       Bytes to write.
	 * @param offset  File offset to write to. Must be >= 0.
	 *
	 * @return _m_result_t where value is the number of bytes written.
	 *
	 * @note Does NOT modify the file position. Safe to call concurrently on the
	 *       same handle from multiple threads.
	 *
	 * @note Writing past EOF extends the file. On POSIX and on Windows, if the
	 *       gap between the previous EOF and the new write is nonzero, that gap
	 *       becomes a hole (reads as zeros) on filesystems that support sparse
	 *       files. On Windows, the file must be marked sparse for the gap to
	 *       actually be sparse; otherwise the OS allocates zeros.
	 */
	_m_result_t _m_pwrite(_m_handle_t h, const void* buf, _m_size_t n, _m_size_t offset);

	/**
	 * @brief Reposition the file offset for subsequent read/write calls.
	 *
	 * @param h       An open handle.
	 * @param offset  Signed offset from `whence`. May be negative.
	 * @param whence  _M_SEEK_SET, _M_SEEK_CUR, or _M_SEEK_END.
	 *
	 * @return _m_result_t where value is the new absolute file position.
	 *
	 * @note Values for `whence` match POSIX (SEEK_SET=0, SEEK_CUR=1, SEEK_END=2)
	 *       AND Windows (FILE_BEGIN=0, FILE_CURRENT=1, FILE_END=2), so no
	 *       translation is needed on any platform.
	 */
	_m_result_t _m_seek(_m_handle_t h, _m_size_t offset, int whence);

#ifdef __cplusplus
}
#endif

#endif /* M__M_IO_H */