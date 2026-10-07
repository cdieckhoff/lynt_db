/**
 * @file _m_pwrite.c
 * @brief Positional write: write at an explicit offset without moving the
 *        file pointer.
 *
 * POSIX:   pwrite(2). Atomic with respect to the file position.
 *
 * Windows: WriteFile with an OVERLAPPED carrying the offset. Same pattern
 *          as _m_pread.
 *
 * Writing past EOF extends the file. If the file is sparse-marked and the
 * gap between the old EOF and the new write is nonzero, the gap becomes a
 * hole. Without sparse marking, the OS allocates zeroed blocks for the gap.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_pwrite(_m_handle_t h, const void* buf, _m_size_t n, _m_size_t offset) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pwrite: invalid handle");
    }
    if (n < 0 || offset < 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pwrite: negative n or offset");
    }
    if (n == 0) {
        return _m_ok(0);
    }
    if (buf == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_pwrite: buf is NULL with n>0");
    }

#ifdef _WIN32
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.Offset = (DWORD)((uint64_t)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);

    DWORD want = (n > (int64_t)0xFFFFFFFF) ? 0xFFFFFFFFu : (DWORD)n;
    DWORD put = 0;

    if (!WriteFile((HANDLE)(intptr_t)h, buf, want, &put, &ov)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_pwrite: WriteFile failed at offset=%lld (error=%lu)",
            (long long)offset, (unsigned long)err);
    }
    return _m_ok((int64_t)put);
#else
    ssize_t put;
    do {
        put = pwrite((int)(h & 0xFFFFFFFF), buf, (size_t)n, (off_t)offset);
    } while (put < 0 && errno == EINTR);

    if (put < 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_pwrite: pwrite failed at offset=%lld (errno=%d)",
            (long long)offset, err);
    }
    return _m_ok((int64_t)put);
#endif
}