/**
 * @file _m_is_hole.c
 * @brief Query whether a region is a hole.
 *
 * Linux:   lseek(fd, offset, SEEK_DATA). If it returns ENXIO, the rest of
 *          the file from `offset` is a hole. If it returns a position > offset,
 *          there is a hole between offset and that position. If it returns
 *          offset itself, the range starts with data.
 *
 * macOS:   fcntl(F_LOG2PHYS) maps a file offset to a physical extent.
 *          A devoffset of -1 indicates a hole. Best-effort; not all macOS
 *          versions support this.
 *
 * Windows: FSCTL_QUERY_ALLOCATED_RANGES returns a list of allocated ranges
 *          within a query range. If the returned list is empty, the entire
 *          query range is unallocated (a hole).
 */

#include "m/_m.h"
#include "m/_m.h"

#include <string.h>   /* for memset */

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#elif defined(__linux__)
#  include <unistd.h>
#  include <errno.h>
#elif defined(__APPLE__)
#  include <fcntl.h>
#  include <errno.h>
#endif

_m_result_t _m_is_hole(_m_handle_t h, _m_size_t offset, _m_size_t length, bool* out) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID || out == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_is_hole: invalid argument");
    }
    if (offset < 0 || length <= 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_is_hole: bad offset or length");
    }

    *out = false;

#ifdef _WIN32
    FILE_ALLOCATED_RANGE_BUFFER query;
    query.FileOffset.QuadPart = (LONGLONG)offset;
    query.Length.QuadPart = (LONGLONG)length;

    /* A single output slot is enough to detect whether anything is allocated. */
    FILE_ALLOCATED_RANGE_BUFFER result;
    DWORD bytesReturned = 0;

    BOOL ok = DeviceIoControl((HANDLE)(intptr_t)h,
        FSCTL_QUERY_ALLOCATED_RANGES,
        &query, sizeof(query),
        &result, sizeof(result),
        &bytesReturned, NULL);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_MORE_DATA) {
            /* There are allocated ranges; not a hole. */
            *out = false;
            return _m_ok(0);
        }
        if (err == ERROR_INVALID_FUNCTION) {
            _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
                "_m_is_hole: filesystem does not support query");
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_is_hole: FSCTL_QUERY_ALLOCATED_RANGES failed (error=%lu)",
            (unsigned long)err);
    }

    /* If no bytes were returned, the entire query range is unallocated. */
    *out = (bytesReturned == 0);
    return _m_ok(0);

#elif defined(__linux__)
    int fd = (int)(h & 0xFFFFFFFF);
    off_t data = lseek(fd, (off_t)offset, SEEK_DATA);
    if (data == (off_t)-1) {
        if (errno == ENXIO) {
            /* No data from offset to EOF; entire range is a hole. */
            *out = true;
            return _m_ok(0);
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_is_hole: lseek(SEEK_DATA) failed (errno=%d)",
            errno);
    }

    /*
     * If the first data byte is at or past the end of our query range,
     * the entire range is a hole.
     */
    if ((int64_t)data >= offset + length) {
        *out = true;
        return _m_ok(0);
    }

    /*
     * If the first data byte is past our offset, there is a hole at the
     * start of the range, but data begins within it. The range as a whole
     * is not a pure hole.
     */
    *out = false;
    return _m_ok(0);

#elif defined(__APPLE__)
    struct log2phys l2p;
    memset(&l2p, 0, sizeof(l2p));
    l2p.l2p_contigbytes = (off_t)length;
    l2p.l2p_devoffset = (off_t)offset;

    if (fcntl((int)(h & 0xFFFFFFFF), F_LOG2PHYS, &l2p) == -1) {
        /* Not supported; report unknown as not-a-hole. */
        *out = false;
        return _m_ok(0);
    }
    *out = (l2p.l2p_devoffset == (off_t)-1);
    return _m_ok(0);

#else
    _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
        "_m_is_hole: not implemented on this platform");
#endif
}