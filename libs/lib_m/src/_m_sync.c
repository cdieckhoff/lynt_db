/**
 * @file _m_sync.c
 * @brief Durability barrier: flush the file's data and metadata to disk.
 *
 * POSIX fsync(2) and Windows FlushFileBuffers both force the OS to commit
 * the file's contents (and, on most filesystems, its metadata) to stable
 * storage. They are expensive. Call them at checkpoint boundaries, not
 * after every write.
 *
 * Note: this is for the FILE's data, accessed via read/write or a shared
 * mmap. For flushing a specific memory-mapped region, use _m_msync, which
 * lives in _m_map.c alongside the other mapping primitives.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

_m_result_t _m_sync(_m_handle_t h) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_sync: invalid handle");
    }

#ifdef _WIN32
    if (!FlushFileBuffers((HANDLE)(intptr_t)h)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_sync: FlushFileBuffers failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);
#else
    if (fsync((int)(h & 0xFFFFFFFF)) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO, "_m_sync: fsync failed (errno=%d)", err);
    }
    return _m_ok(0);
#endif
}