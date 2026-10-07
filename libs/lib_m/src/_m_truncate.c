/**
 * @file _m_truncate.c
 * @brief Resize a file to a given length.
 *
 * GROWING A FILE
 * --------------
 * On POSIX, ftruncate(fd, larger) extends the file and the gap reads as
 * zeros. On filesystems that support sparse files, the gap is NOT actually
 * allocated on disk; it is a hole. This is the primitive a database uses
 * to preallocate capacity without consuming disk.
 *
 * On Windows, SetEndOfFile extends the file. The gap is zeros, and if the
 * file is sparse-marked, the gap is a hole. If not sparse-marked, the OS
 * allocates zeroed blocks. See _m_mark_sparse.
 *
 * SHRINKING A FILE
 * ----------------
 * Both platforms discard the data past the new length. Any mmap of the
 * discarded region becomes invalid on both platforms; callers must unmap
 * before truncating.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_truncate(_m_handle_t h, _m_size_t size) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_truncate: invalid handle");
    }
    if (size < 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_truncate: negative size %lld",
            (long long)size);
    }

#ifdef _WIN32
    HANDLE fh = (HANDLE)(intptr_t)h;

    /*
     * SetFilePointerEx moves the file pointer; SetEndOfFile then cuts the
     * file at that position. Two calls are required because Windows does
     * not have a one-call equivalent of ftruncate.
     *
     * We use FILE_BEGIN so the pointer lands exactly at `size` regardless
     * of the current position.
     */
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;

    if (!SetFilePointerEx(fh, li, NULL, FILE_BEGIN)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_truncate: SetFilePointerEx failed (error=%lu)",
            (unsigned long)err);
    }
    if (!SetEndOfFile(fh)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_truncate: SetEndOfFile failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);
#else
    int fd = (int)(h & 0xFFFFFFFF);
    if (ftruncate(fd, (off_t)size) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_truncate: ftruncate failed (errno=%d)", err);
    }
    return _m_ok(0);
#endif
}