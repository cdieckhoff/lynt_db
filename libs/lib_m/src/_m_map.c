/**
 * @file _m_map.c
 * @brief Memory-map a region of a file, presenting POSIX semantics on
 *        every platform.
 *
 * WHAT THIS FILE DOES
 * -------------------
 * Implements _m_map(), _m_unmap(), _m_msync(), and _m_mapping_addr().
 * These present POSIX mmap semantics uniformly on Linux, macOS, and
 * Windows.
 *
 * The two platform-specific quirks this file hides:
 *
 *   1. Windows requires MapViewOfFileEx's offset to be a multiple of
 *      dwAllocationGranularity (64 KiB). POSIX requires only page
 *      alignment. We align down, extend the length, and return a pointer
 *      into the mapping so callers never see the difference.
 *
 *   2. Neither platform guarantees that mapping past EOF is safe to
 *      access without extending the file first:
 *
 *        Windows: CreateFileMapping refuses to map past EOF. We extend
 *                 the file with SetEndOfFile before creating the mapping.
 *
 *        POSIX:   mmap accepts the mapping, but any access to a whole
 *                 page past EOF raises SIGBUS. We extend the file with
 *                 ftruncate before calling mmap, so the mapped region
 *                 is always backed by file pages. This matches Windows
 *                 semantics and gives both platforms the same behavior:
 *                 reading past EOF returns zeros, writing past EOF
 *                 extends the file.
 *
 * The mapping bookkeeping (base pointer, adjusted length, user-visible
 * pointer, adjusted offset) is stored in an _m_mapping_t which callers
 * treat as opaque. This is what lets _m_unmap and _m_msync do the right
 * thing on Windows, where UnmapViewOfFile and FlushViewOfFile take only
 * the base address, not the caller's user pointer.
 */

#include "m/_m.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <errno.h>
#endif

 /* ====================================================================
  * POSIX implementation
  * ==================================================================== */

#ifndef _WIN32

  /**
   * @brief Ensure the file is at least `needed` bytes long.
   *
   * Called before mmap to guarantee that the region we're about to map is
   * backed by actual file pages. Without this, mapping past EOF succeeds
   * (POSIX allows it) but the first access to a page entirely past EOF
   * raises SIGBUS. Extending the file first makes the mapping safe.
   *
   * If the file is already large enough, this is a cheap fstat and no
   * ftruncate. On a growable file, ftruncate extends by creating a hole
   * on filesystems that support sparse files, so growing to 128 KiB does
   * not consume 128 KiB of disk on ext4, xfs, btrfs, APFS, or HFS+.
   */
static _m_result_t ensure_file_covers(int fd, _m_size_t needed) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return _m_err(_M_ERR_IO);
    }
    if ((_m_size_t)st.st_size >= needed) {
        return _m_ok(0);
    }
    if (ftruncate(fd, (off_t)needed) != 0) {
        return _m_err(_M_ERR_IO);
    }
    return _m_ok(0);
}

_m_ptr_result_t _m_map(void* addr, _m_size_t len, int prot, int flags,
    _m_handle_t h, _m_size_t offset) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG, "_m_map: invalid handle");
    }
    if (len <= 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_map: length must be > 0 (got %lld)",
            (long long)len);
    }
    if (offset < 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_map: offset must be >= 0 (got %lld)",
            (long long)offset);
    }

    /*
     * POSIX requires offset to be page-aligned. The API contract says
     * callers may pass any offset, but on POSIX the kernel will reject
     * unaligned offsets, so we validate and return a clear error rather
     * than letting mmap fail with a confusing errno.
     */
    long pgsz = sysconf(_SC_PAGESIZE);
    if (pgsz <= 0) pgsz = 4096;
    if ((offset % (int64_t)pgsz) != 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_map: offset %lld not page-aligned (page=%ld)",
            (long long)offset, pgsz);
    }

    int fd = (int)(h & 0xFFFFFFFF);

    /*
     * Extend the file to cover [offset, offset+len). This is what makes
     * mapping past EOF safe and matches Windows behavior. Without this,
     * reading a page entirely past EOF raises SIGBUS on Linux and macOS.
     */
    {
        _m_result_t ext = ensure_file_covers(fd, offset + len);
        if (ext.code != _M_OK) {
            int err = errno;
            _M_RETURN_ERR_PTR(_M_ERR_IO,
                "_m_map: failed to extend file to %lld bytes "
                "(errno=%d)",
                (long long)(offset + len), err);
        }
    }

    int mmap_prot = 0;
    if (prot & _M_PROT_READ)  mmap_prot |= PROT_READ;
    if (prot & _M_PROT_WRITE) mmap_prot |= PROT_WRITE;
    if (prot & _M_PROT_EXEC)  mmap_prot |= PROT_EXEC;

    int mmap_flags = (flags & _M_MAP_PRIVATE) ? MAP_PRIVATE : MAP_SHARED;

    void* base = mmap(addr, (size_t)len, mmap_prot, mmap_flags,
        fd, (off_t)offset);

    if (base == MAP_FAILED) {
        int err = errno;
        _M_RETURN_ERR_PTR(_M_ERR_MM_FAILED,
            "_m_map: mmap failed at offset=%lld len=%lld (errno=%d)",
            (long long)offset, (long long)len, err);
    }

    _m_mapping_t* m = (_m_mapping_t*)malloc(sizeof(_m_mapping_t));
    if (m == NULL) {
        munmap(base, (size_t)len);
        _M_RETURN_ERR_PTR(_M_ERR_NOMEM,
            "_m_map: out of memory for mapping handle");
    }

    m->base = base;
    m->aligned_len = len;
    m->user_ptr = base;   /* No adjustment on POSIX. */
    m->aligned_off = offset;

    return _m_ok_ptr(m);
}

void* _m_mapping_addr(_m_mapping_t* m) {
    return (m != NULL) ? m->user_ptr : NULL;
}

_m_result_t _m_unmap(_m_mapping_t* m) {
    _m_clear_error();

    if (m == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_unmap: mapping is NULL");
    }

    if (munmap(m->base, (size_t)m->aligned_len) != 0) {
        int err = errno;
        free(m);
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_unmap: munmap failed (errno=%d)", err);
    }

    free(m);
    return _m_ok(0);
}

_m_result_t _m_msync(_m_mapping_t* m) {
    _m_clear_error();

    if (m == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_msync: mapping is NULL");
    }

    if (msync(m->base, (size_t)m->aligned_len, MS_SYNC) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_msync: msync failed (errno=%d)", err);
    }
    return _m_ok(0);
}

/* ====================================================================
 * Windows implementation
 * ==================================================================== */
#else  /* _WIN32 */

static _m_size_t align_down(_m_size_t v, _m_size_t granularity) {
    return v & ~(granularity - 1);
}

_m_ptr_result_t _m_map(void* addr, _m_size_t len, int prot, int flags,
    _m_handle_t h, _m_size_t offset) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG, "_m_map: invalid handle");
    }
    if (len <= 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_map: length must be > 0 (got %lld)",
            (long long)len);
    }
    if (offset < 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_map: offset must be >= 0 (got %lld)",
            (long long)offset);
    }

    HANDLE fh = (HANDLE)(intptr_t)h;

    /* Extend the file if the mapping goes past EOF. Same rationale as
     * POSIX: CreateFileMapping refuses to map past EOF, so we make the
     * file large enough first. */
    {
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(fh, &sz)) {
            DWORD err = GetLastError();
            _M_RETURN_ERR_PTR(_M_ERR_IO,
                "_m_map: GetFileSizeEx failed (error=%lu)",
                (unsigned long)err);
        }
        _m_size_t current_size = (_m_size_t)sz.QuadPart;
        _m_size_t needed = offset + len;
        if (needed > current_size) {
            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)needed;
            if (!SetFilePointerEx(fh, li, NULL, FILE_BEGIN)) {
                DWORD err = GetLastError();
                _M_RETURN_ERR_PTR(_M_ERR_IO,
                    "_m_map: SetFilePointerEx failed (error=%lu)",
                    (unsigned long)err);
            }
            if (!SetEndOfFile(fh)) {
                DWORD err = GetLastError();
                _M_RETURN_ERR_PTR(_M_ERR_IO,
                    "_m_map: SetEndOfFile failed (error=%lu)",
                    (unsigned long)err);
            }
        }
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    _m_size_t gran = (_m_size_t)si.dwAllocationGranularity;

    _m_size_t aligned_off = align_down(offset, gran);
    _m_size_t intra_off = offset - aligned_off;
    _m_size_t aligned_len = intra_off + len;

    DWORD flProtect = (prot & _M_PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
    DWORD dwAccess = (prot & _M_PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;

    HANDLE hMap = CreateFileMappingA(fh, NULL, flProtect, 0, 0, NULL);
    if (hMap == NULL) {
        DWORD err = GetLastError();
        _M_RETURN_ERR_PTR(_M_ERR_MM_FAILED,
            "_m_map: CreateFileMapping failed (error=%lu)",
            (unsigned long)err);
    }

    void* base = MapViewOfFileEx(hMap, dwAccess,
        (DWORD)((uint64_t)aligned_off >> 32),
        (DWORD)((uint64_t)aligned_off & 0xFFFFFFFFu),
        (SIZE_T)aligned_len, addr);

    DWORD last = GetLastError();
    CloseHandle(hMap);

    if (base == NULL) {
        _M_RETURN_ERR_PTR(_M_ERR_MM_FAILED,
            "_m_map: MapViewOfFileEx failed at "
            "offset=%lld (aligned to %lld) len=%lld (error=%lu)",
            (long long)offset, (long long)aligned_off,
            (long long)aligned_len, (unsigned long)last);
    }

    _m_mapping_t* m = (_m_mapping_t*)malloc(sizeof(_m_mapping_t));
    if (m == NULL) {
        UnmapViewOfFile(base);
        _M_RETURN_ERR_PTR(_M_ERR_NOMEM,
            "_m_map: out of memory for mapping handle");
    }

    m->base = base;
    m->aligned_len = aligned_len;
    m->user_ptr = (uint8_t*)base + intra_off;
    m->aligned_off = aligned_off;

    return _m_ok_ptr(m);
}

void* _m_mapping_addr(_m_mapping_t* m) {
    return (m != NULL) ? m->user_ptr : NULL;
}

_m_result_t _m_unmap(_m_mapping_t* m) {
    _m_clear_error();

    if (m == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_unmap: mapping is NULL");
    }

    if (!UnmapViewOfFile(m->base)) {
        DWORD err = GetLastError();
        free(m);
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_unmap: UnmapViewOfFile failed (error=%lu)",
            (unsigned long)err);
    }

    free(m);
    return _m_ok(0);
}

_m_result_t _m_msync(_m_mapping_t* m) {
    _m_clear_error();

    if (m == NULL) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG, "_m_msync: mapping is NULL");
    }

    if (!FlushViewOfFile(m->base, (SIZE_T)m->aligned_len)) {
        DWORD err = GetLastError();
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_msync: FlushViewOfFile failed (error=%lu)",
            (unsigned long)err);
    }
    return _m_ok(0);
}

#endif  /* _WIN32 */