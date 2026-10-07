/**
 * @file _m_read.c
 * @brief Sequential read at the current file position.
 *
 * POSIX:   read(2).
 * Windows: ReadFile with no OVERLAPPED, which reads from the current file
 *          pointer and advances it.
 *
 * Short reads (value < n) are NOT errors. They indicate EOF or a partial
 * read. Callers that need exactly n bytes must loop.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_read(_m_handle_t h, void* buf, _m_size_t n) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_read: invalid handle");
    }
    if (n < 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_read: negative length");
    }
    if (n == 0) {
        /* POSIX read(fd, buf, 0) returns 0 with no side effects. */
        return _m_ok(0);
    }
    if (buf == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_read: buf is NULL with n>0");
    }

#ifdef _WIN32
    /*
     * ReadFile takes a DWORD for the length. For n > 4 GiB we'd need to
     * loop, but individual reads that large are pathological. We cap at
     * DWORD_MAX and let the caller loop if they need more.
     */
    DWORD want = (n > (int64_t)0xFFFFFFFF) ? 0xFFFFFFFFu : (DWORD)n;
    DWORD got = 0;

    if (!ReadFile((HANDLE)(intptr_t)h, buf, want, &got, NULL)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_read: ReadFile failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok((int64_t)got);
#else
    /*
     * POSIX read can be interrupted by a signal. EINTR should be retried;
     * the caller usually doesn't care that a signal arrived.
     */
    ssize_t got;
    do {
        got = read((int)(h & 0xFFFFFFFF), buf, (size_t)n);
    } while (got < 0 && errno == EINTR);

    if (got < 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_read: read failed (errno=%d)", err);
    }
    return _m_ok((int64_t)got);
#endif
}