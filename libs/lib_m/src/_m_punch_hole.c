/**
 * @file _m_punch_hole.c
 * @brief Deallocate a region of a file, leaving a hole.
 *
 * Linux:   fallocate(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE).
 * macOS:   fcntl(F_PUNCHHOLE) with a fpunchhole_t.
 * Windows: FSCTL_SET_ZERO_DATA. The file must have been marked sparse
 *          (FSCTL_SET_SPARSE) first, or the operation just writes zeros.
 *
 * Alignment requirements:
 *   Linux:   offset and length must be multiples of the filesystem block
 *            size, typically 4096. xfs historically required length to be
 *            a multiple of 512 at minimum.
 *   macOS:   offset must be page-aligned; length is rounded up.
 *   Windows: offset and length must be cluster-aligned (typically 4096).
 *
 * We check alignment against _m_alloc_granularity and reject unaligned
 * calls with _M_ERR_INVALID_ARG. Callers should query the granularity
 * once and round their ranges accordingly.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#elif defined(__linux__)
#  include <linux/falloc.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <fcntl.h>
#  include <errno.h>
#  include <unistd.h>
#  include <string.h>
#endif

_m_result_t _m_punch_hole(_m_handle_t h, _m_size_t offset, _m_size_t length) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_punch_hole: invalid handle");
    }
    if (offset < 0 || length <= 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_punch_hole: bad offset or length");
    }

    /*
     * Alignment check. Use the granularity, which is 64 KiB on Windows
     * and page size on POSIX. Real filesystems allow finer alignment for
     * hole punching, but requiring the coarse alignment keeps callers
     * portable.
     */
    _m_size_t align = _m_alloc_granularity();
    if (align <= 0) align = 4096;
    if ((offset % align) != 0 || (length % align) != 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_punch_hole: offset=%lld length=%lld not aligned to %lld",
            (long long)offset, (long long)length, (long long)align);
    }

#ifdef _WIN32
    FILE_ZERO_DATA_INFORMATION fzdi;
    fzdi.FileOffset.QuadPart = (LONGLONG)offset;
    fzdi.BeyondFinalZero.QuadPart = (LONGLONG)(offset + length);

    DWORD bytesReturned = 0;
    if (!DeviceIoControl((HANDLE)(intptr_t)h, FSCTL_SET_ZERO_DATA,
        &fzdi, sizeof(fzdi),
        NULL, 0, &bytesReturned, NULL)) {
        DWORD err = GetLastError();
        /*
         * ERROR_INVALID_FUNCTION means the filesystem does not support
         * sparse operations (FAT, exFAT, some network FSes). Map to
         * _M_ERR_NOT_SUPPORTED so callers can degrade gracefully.
         */
        if (err == ERROR_INVALID_FUNCTION) {
            _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
                "_m_punch_hole: filesystem does not support sparse");
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_punch_hole: FSCTL_SET_ZERO_DATA failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);

#elif defined(__linux__)
    int ret = fallocate((int)(h & 0xFFFFFFFF),
        FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
        (off_t)offset, (off_t)length);
    if (ret != 0) {
        int err = errno;
        if (err == EOPNOTSUPP) {
            _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
                "_m_punch_hole: filesystem does not support PUNCH_HOLE");
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_punch_hole: fallocate failed (errno=%d)", err);
    }
    return _m_ok(0);

#elif defined(__APPLE__)
    fpunchhole_t fp;
    memset(&fp, 0, sizeof(fp));
    fp.fp_offset = (off_t)offset;
    fp.fp_length = (off_t)length;

    if (fcntl((int)(h & 0xFFFFFFFF), F_PUNCHHOLE, &fp) == -1) {
        int err = errno;
        if (err == ENOTSUP) {
            _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
                "_m_punch_hole: filesystem does not support F_PUNCHHOLE");
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_punch_hole: fcntl(F_PUNCHHOLE) failed (errno=%d)", err);
    }
    return _m_ok(0);

#else
    _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
        "_m_punch_hole: not implemented on this platform");
#endif
}