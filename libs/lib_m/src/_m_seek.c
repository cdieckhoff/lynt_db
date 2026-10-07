/**
 * @file _m_seek.c
 * @brief Reposition the file offset.
 *
 * POSIX:   lseek(2). The whence values are SEEK_SET=0, SEEK_CUR=1, SEEK_END=2.
 * Windows: SetFilePointerEx. The whence values are FILE_BEGIN=0,
 *          FILE_CURRENT=1, FILE_END=2.
 *
 * The numeric values coincide, so no translation is needed. We pass our
 * _M_SEEK_* values straight through on both platforms.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_seek(_m_handle_t h, _m_size_t offset, int whence) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_seek: invalid handle");
    }
    if (whence != _M_SEEK_SET && whence != _M_SEEK_CUR && whence != _M_SEEK_END) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_seek: invalid whence %d", whence);
    }

#ifdef _WIN32
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;

    LARGE_INTEGER out;
    if (!SetFilePointerEx((HANDLE)(intptr_t)h, li, &out, (DWORD)whence)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_seek: SetFilePointerEx failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok((int64_t)out.QuadPart);
#else
    off_t pos = lseek((int)(h & 0xFFFFFFFF), (off_t)offset, whence);
    if (pos == (off_t)-1) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_seek: lseek failed (errno=%d)", err);
    }
    return _m_ok((int64_t)pos);
#endif
}