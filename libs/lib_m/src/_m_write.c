/**
 * @file _m_write.c
 * @brief Sequential write at the current file position.
 *
 * POSIX:   write(2).
 * Windows: WriteFile with no OVERLAPPED.
 *
 * Like _m_read, a short write is not an error. Callers that need exactly
 * n bytes must loop. This is rare on regular files (the kernel writes
 * everything or fails) but common on pipes and sockets. Since we only
 * support regular files, a short write usually indicates a disk-full
 * condition on the next write, not this one.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_write(_m_handle_t h, const void* buf, _m_size_t n) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_write: invalid handle");
    }
    if (n < 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_write: negative length");
    }
    if (n == 0) {
        return _m_ok(0);
    }
    if (buf == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_write: buf is NULL with n>0");
    }

#ifdef _WIN32
    DWORD want = (n > (int64_t)0xFFFFFFFF) ? 0xFFFFFFFFu : (DWORD)n;
    DWORD put = 0;

    if (!WriteFile((HANDLE)(intptr_t)h, buf, want, &put, NULL)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_write: WriteFile failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok((int64_t)put);
#else
    ssize_t put;
    do {
        put = write((int)(h & 0xFFFFFFFF), buf, (size_t)n);
    } while (put < 0 && errno == EINTR);

    if (put < 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_write: write failed (errno=%d)", err);
    }
    return _m_ok((int64_t)put);
#endif
}