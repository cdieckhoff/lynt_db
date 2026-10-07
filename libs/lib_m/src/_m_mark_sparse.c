/**
 * @file _m_mark_sparse.c
 * @brief Mark a file as sparse.
 *
 * SPARSE FILES
 * ------------
 * A sparse file has regions that read as zeros but occupy no disk
 * blocks. For a database that preallocates large regions via
 * _m_truncate, sparse files let logical size and physical size diverge:
 * you can promise the OS a 1 TiB file while only consuming disk space
 * for the pages you actually write.
 *
 * PLATFORM BEHAVIOR
 * -----------------
 * Linux/macOS: Sparse files are created implicitly. Extending a file
 *              past EOF via ftruncate, or writing with gaps, produces
 *              holes on filesystems that support sparse files (ext4,
 *              xfs, btrfs, APFS, HFS+). There is no per-file flag to
 *              set, so this function is a no-op that always succeeds.
 *
 * Windows:     Sparse is a per-file attribute that must be set before
 *              FSCTL_SET_ZERO_DATA will deallocate blocks. Without it,
 *              "punching a hole" writes actual zeros and consumes disk
 *              space. Only NTFS supports this; FAT, exFAT, and most
 *              network filesystems return ERROR_INVALID_FUNCTION, which
 *              we map to _M_ERR_NOT_SUPPORTED so callers can degrade
 *              gracefully.
 *
 * WHY IT'S OK FOR THIS TO FAIL
 * ----------------------------
 * Sparse support is best-effort. A caller that wants sparse behavior
 * should call this function, check the return, and continue either way.
 * The file will still work correctly without sparse marking; it will
 * just consume more disk space than strictly necessary.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#endif

_m_result_t _m_mark_sparse(_m_handle_t h) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_mark_sparse: invalid handle");
    }

#ifdef _WIN32
    /*
     * FSCTL_SET_SPARSE takes no input buffer and produces no output.
     * DeviceIoControl requires non-NULL pointers only when the
     * corresponding size is nonzero, so passing NULL, 0 is valid.
     */
    DWORD bytesReturned = 0;
    if (!DeviceIoControl((HANDLE)(intptr_t)h, FSCTL_SET_SPARSE,
        NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        DWORD err = GetLastError();

        /*
         * ERROR_INVALID_FUNCTION means the filesystem doesn't know the
         * ioctl at all (FAT, exFAT, some SMB shares). Map to
         * _M_ERR_NOT_SUPPORTED so callers can continue without sparse
         * semantics.
         */
        if (err == ERROR_INVALID_FUNCTION) {
            _M_RETURN_ERR(_M_ERR_NOT_SUPPORTED,
                "_m_mark_sparse: filesystem does not support "
                "sparse files");
        }
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_mark_sparse: FSCTL_SET_SPARSE failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);
#else
    /*
     * POSIX: no flag to set. Sparse behavior is a filesystem property,
     * not a file property. ext4, xfs, btrfs, APFS, and HFS+ all support
     * it transparently.
     */
    return _m_ok(0);
#endif
}