/**
 * @file _m_size.c
 * @brief Query the logical size of a file.
 *
 * On POSIX, we call fstat and read st_size.
 * On Windows, we call GetFileSizeEx and read the LARGE_INTEGER.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <errno.h>
#endif

_m_result_t _m_size(_m_handle_t h) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_size: invalid handle");
    }

#ifdef _WIN32
    LARGE_INTEGER li;
    if (!GetFileSizeEx((HANDLE)(intptr_t)h, &li)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_size: GetFileSizeEx failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok((int64_t)li.QuadPart);
#else
    struct stat st;
    if (fstat((int)(h & 0xFFFFFFFF), &st) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_size: fstat failed (errno=%d)", err);
    }
    return _m_ok((int64_t)st.st_size);
#endif
}