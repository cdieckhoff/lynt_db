/**
 * @file _m_pread.c
 * @brief Positional read: read at an explicit offset without moving the
 *        file pointer.
 *
 * POSIX:   pread(2). Atomic with respect to the file position, so safe to
 *          call concurrently from multiple threads on the same handle.
 *
 * Windows: ReadFile with an OVERLAPPED struct carrying the offset. No
 *          FILE_FLAG_OVERLAPPED is needed on the handle; a synchronous
 *          handle performs a blocking read at the specified offset and
 *          leaves the file pointer untouched. This gives us pread
 *          semantics on Windows with no extra machinery.
 *
 * This is the primitive that a database should use for all non-mapped I/O.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_pread(_m_handle_t h, void* buf, _m_size_t n, _m_size_t offset) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pread: invalid handle");
    }
    if (n < 0 || offset < 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pread: negative n or offset");
    }
    if (n == 0) {
        return _m_ok(0);
    }
    if (buf == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pread: buf is NULL with n>0");
    }

#ifdef _WIN32
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.Offset = (DWORD)((uint64_t)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);

    DWORD want = (n > (int64_t)0xFFFFFFFF) ? 0xFFFFFFFFu : (DWORD)n;
    DWORD got = 0;

    if (!ReadFile((HANDLE)(intptr_t)h, buf, want, &got, &ov)) {
        DWORD err = GetLastError();
        /*
         * ERROR_HANDLE_EOF is not a failure; it means the offset was past
         * the end of the file. Return 0 bytes read, which is what POSIX
         * pread does in the same situation.
         */
        if (err == ERROR_HANDLE_EOF) {
            return _m_ok(0);
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_pread: ReadFile failed at offset=%lld (error=%lu)",
            (long long)offset, (unsigned long)err);
    }
    return _m_ok((int64_t)got);
#else
    ssize_t got;
    do {
        got = pread((int)(h & 0xFFFFFFFF), buf, (size_t)n, (off_t)offset);
    } while (got < 0 && errno == EINTR);

    if (got < 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_pread: pread failed at offset=%lld (errno=%d)",
            (long long)offset, err);
    }
    return _m_ok((int64_t)got);
#endif
}