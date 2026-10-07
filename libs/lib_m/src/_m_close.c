/**
 * @file _m_close.c
 * @brief Close a file handle.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_close(_m_handle_t h) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_close: invalid handle");
    }

#ifdef _WIN32
    /*
     * Cast chain: int64_t -> intptr_t (pointer-sized) -> HANDLE (void*).
     * The intermediate intptr_t cast silences the compiler's
     * integer-to-pointer warning and is guaranteed lossless on 64-bit.
     */
    if (!CloseHandle((HANDLE)(intptr_t)h)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_close: CloseHandle failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);
#else
    /*
     * Mask off the upper 32 bits before casting to int. In practice they
     * are always zero (fd was stored zero-extended), but the mask makes
     * the intent explicit and protects against future changes.
     */
    int fd = (int)(h & 0xFFFFFFFF);
    if (close(fd) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_close: close failed (errno=%d)", err);
    }
    return _m_ok(0);
#endif
}