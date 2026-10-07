/**
 * @file _m_open.c
 * @brief Open or create a file with POSIX-style flags.
 *
 * WHAT THIS FILE DOES
 * -------------------
 * Opens a file using POSIX semantics on all platforms.
 *
 * POSIX path: passes flags straight to open(2). No translation needed
 * because _M_O_* values match the POSIX values exactly.
 *
 * Windows path: parses the POSIX flag bits, builds CreateFileW
 * parameters, converts the UTF-8 path to UTF-16, and (if the caller
 * requested sparse) sets FILE_ATTRIBUTE_SPARSE_FILE on the created
 * file. Non-ASCII paths work correctly on Windows via CreateFileW.
 *
 * SPARSE FILES
 * ------------
 * The `sparse` argument requests sparse-file behavior:
 *
 *   - On Linux and macOS, this is a no-op. Sparse files are created
 *     implicitly: a file with a hole (from truncate past EOF, or
 *     pwrite with a gap) is sparse if the filesystem supports it. There
 *     is no per-file flag to set.
 *
 *   - On Windows, sparse is a per-file attribute. It must be set at
 *     create time or via FSCTL_SET_SPARSE. We set it here with
 *     FILE_ATTRIBUTE_SPARSE_FILE. If the filesystem does not support
 *     sparse files (FAT, exFAT, some network filesystems), the flag is
 *     ignored and the file opens normally. This matches POSIX behavior
 *     where sparse is best-effort.
 *
 * Why does sparse matter?
 *   For a database that truncates to a large size (say 1 TiB) and only
 *   writes a few pages, the file consumes 1 TiB of disk unless it's
 *   sparse. Sparse files let logical size and physical size diverge.
 */

#include "m/_m.h"

#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#endif

 /* ====================================================================
  * Windows backend
  * ==================================================================== */
#ifdef _WIN32

  /**
   * @brief Translate a Windows error code to an _m_err_t.
   */
static int32_t _m_win32_errno_to_code(DWORD err) {
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return _M_ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return _M_ERR_PERMISSION;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return _M_ERR_ALREADY_EXISTS;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return _M_ERR_NOMEM;
    case ERROR_INVALID_PARAMETER:
        return _M_ERR_INVALID_ARG;
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_FUNCTION:
        return _M_ERR_NOT_SUPPORTED;
    default:
        return _M_ERR_IO;
    }
}

/**
 * @brief Convert UTF-8 to UTF-16 into a caller-provided buffer.
 *
 * @return Length in wchar_t (including NUL) on success, 0 on failure.
 */
static int _m_utf8_to_utf16(const char* utf8, wchar_t* wbuf, int wcap) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, wcap);
}

_m_result_t _m_open(const char* path, int flags, int mode, bool sparse) {
    _m_clear_error();
    (void)mode;   /* Windows uses ACLs, not POSIX mode bits. */

    if (path == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_open: path is NULL");
    }

    /* ---- Translate POSIX flags to CreateFileW access bits ---- */
    DWORD access = 0;
    if ((flags & _M_O_RDWR) == _M_O_RDWR) {
        access = GENERIC_READ | GENERIC_WRITE;
    }
    else if (flags & _M_O_WRONLY) {
        access = GENERIC_WRITE;
    }
    else {
        access = GENERIC_READ;
    }

    if (flags & _M_O_APPEND) {
        access |= FILE_APPEND_DATA;
    }

    /* ---- Translate POSIX flags to CreateFileW creation disposition ---- */
    DWORD creation = OPEN_EXISTING;
    if ((flags & _M_O_CREAT) && (flags & _M_O_EXCL)) {
        creation = CREATE_NEW;
    }
    else if ((flags & _M_O_CREAT) && (flags & _M_O_TRUNC)) {
        creation = CREATE_ALWAYS;
    }
    else if (flags & _M_O_CREAT) {
        creation = OPEN_ALWAYS;
    }
    else if (flags & _M_O_TRUNC) {
        creation = TRUNCATE_EXISTING;
    }

    /* ---- Share modes: allow concurrent access for mmap ---- */
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    /* ---- Attributes: sparse if requested, always random access ---- */
    DWORD attrs = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;

    if (sparse) {
        /*
         * FILE_ATTRIBUTE_SPARSE_FILE must be combined with
         * FILE_ATTRIBUTE_NORMAL cleared or set; on NTFS, the sparse
         * attribute overrides normal. If the filesystem doesn't support
         * sparse, CreateFileW will still succeed but the attribute will
         * be ignored. That's what we want.
         */
        attrs = FILE_ATTRIBUTE_SPARSE_FILE | FILE_FLAG_RANDOM_ACCESS;
    }

    /* ---- Convert UTF-8 path to UTF-16 ---- */
    int wlen = _m_utf8_to_utf16(path, NULL, 0);
    if (wlen <= 0) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_open: path is not valid UTF-8: '%s'", path);
    }

    wchar_t stackbuf[512];
    wchar_t* wpath = stackbuf;
    if (wlen > (int)(sizeof(stackbuf) / sizeof(stackbuf[0]))) {
        wpath = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
        if (wpath == NULL) {
            _M_RETURN_ERR(_M_ERR_NOMEM, "_m_open: out of memory for path");
        }
    }

    if (_m_utf8_to_utf16(path, wpath, wlen) <= 0) {
        if (wpath != stackbuf) free(wpath);
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_open: UTF-8 to UTF-16 conversion failed");
    }

    /* ---- Actually open ---- */
    HANDLE h = CreateFileW(wpath, access, share, NULL, creation, attrs, NULL);
    DWORD last = GetLastError();

    if (wpath != stackbuf) free(wpath);

    if (h == INVALID_HANDLE_VALUE) {
        _M_RETURN_ERR(_m_win32_errno_to_code(last),
            "_m_open: CreateFileW failed for '%s' (error=%lu)",
            path, (unsigned long)last);
    }

    return _m_ok((int64_t)(intptr_t)h);
}

/* ====================================================================
 * POSIX backend
 * ==================================================================== */
#else

  /**
   * @brief Translate errno to an _m_err_t.
   */
static int32_t _m_posix_errno_to_code(int err) {
    switch (err) {
    case ENOENT:        return _M_ERR_NOT_FOUND;
    case EACCES:
    case EPERM:         return _M_ERR_PERMISSION;
    case EEXIST:        return _M_ERR_ALREADY_EXISTS;
    case ENOMEM:        return _M_ERR_NOMEM;
    case EINVAL:        return _M_ERR_INVALID_ARG;
    case ENOSYS:
    case ENOTSUP:       return _M_ERR_NOT_SUPPORTED;
    default:            return _M_ERR_IO;
    }
}

_m_result_t _m_open(const char* path, int flags, int mode, bool sparse) {
    _m_clear_error();
    (void)sparse;   /* Sparse files are implicit on POSIX. */

    if (path == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_open: path is NULL");
    }

    /*
     * _M_O_* values are chosen to match POSIX exactly, so no
     * translation is needed. We add O_CLOEXEC for hygiene: the
     * database should not leak file descriptors into forked children.
     */
    int fd = open(path, flags | O_CLOEXEC, (mode_t)mode);

    if (fd < 0) {
        int err = errno;
        _M_RETURN_ERR(_m_posix_errno_to_code(err),
            "_m_open: open failed for '%s' (errno=%d)", path, err);
    }

    return _m_ok((int64_t)fd);
}

#endif