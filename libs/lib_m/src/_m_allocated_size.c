/**
 * @file _m_allocated_size.c
 * @brief Bytes actually consumed on disk.
 *
 * WHAT THIS MEASURES
 * ------------------
 * The difference between a file's LOGICAL size (what _m_size returns)
 * and its ALLOCATED size (what this returns) is the whole point of
 * sparse files. A sparse 1 GiB file with only 4 MiB of data written
 * has a logical size of 1 GiB and an allocated size of about 4 MiB.
 *
 * POSIX
 * -----
 * fstat gives st_blocks in 512-byte units. Multiply by 512 to get
 * allocated bytes. This reflects the actual on-disk consumption:
 * filesystem blocks that have been allocated to the file, whether
 * they contain real data or zeros.
 *
 * WINDOWS
 * -------
 * Windows has no one-call equivalent. GetFileSizeEx returns the LOGICAL
 * size, not the allocated size. The correct method is
 * FSCTL_GET_RETRIEVAL_POINTERS, which returns a list of extents. Each
 * extent has a NextVcn (the VCN immediately after the extent) and an
 * Lcn (the logical cluster number where the extent's data lives, or
 * -1 to mark a hole).
 *
 * The semantics of an extent entry are:
 *   - The range it describes is [previous NextVcn, this NextVcn).
 *   - Lcn == -1 marks that range as a hole (unallocated).
 *   - Any other Lcn means the range is backed by real clusters,
 *     starting at the given logical cluster number.
 *
 * So the allocated cluster count is:
 *     sum over extents where Lcn != -1 of (NextVcn - previous NextVcn)
 *
 * This is the detail that makes the algorithm non-obvious: reading
 * only NextVcn overcounts, because holes are also represented as
 * extent boundaries. The Lcn field is what distinguishes them.
 *
 * The ioctl is called in a loop: pass a starting VCN, receive as many
 * extents as fit, and resume from the last VCN received if the buffer
 * was too small (signaled by ERROR_MORE_DATA). We handle that loop and
 * are defensive about the fact that ExtentCount may report the total
 * number of extents rather than the number that fit in the current
 * buffer.
 *
 * The cluster size comes from GetDiskFreeSpaceW on the volume
 * containing the file. We derive the volume root from the file's final
 * path. If that fails for any reason, we fall back to 4096 (NTFS
 * default on most volumes).
 *
 * WHY THE COMPLEXITY IS WORTH IT
 * ------------------------------
 * Without FSCTL_GET_RETRIEVAL_POINTERS, _m_punch_hole looks like it
 * does nothing on Windows — the logical size doesn't change, so a
 * caller checking "did the punch work?" via _m_allocated_size sees a
 * delta of zero. The punch itself is fine; the measurement was wrong.
 * With the retrieval-pointers implementation, the delta becomes visible
 * and _m_punch_hole becomes verifiable on Windows just as it is on
 * Linux and macOS.
 */

#include "m/_m.h"

#include <stddef.h>   /* for offsetof */
#include <wchar.h>    /* for wmemcpy, wcsncmp on Windows */

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#else
#  include <sys/stat.h>
#  include <errno.h>
#endif

#ifdef _WIN32
 /* ====================================================================
  * Windows implementation
  * ==================================================================== */

  /**
   * @brief Derive the volume root from a file handle.
   *
   * On success, writes the volume root (e.g. L"\\?\C:\" or
   * L"\\?\Volume{...}\") into `out`, which must have space for at least
   * MAX_PATH characters. Returns true on success.
   *
   * The technique: get the final path via GetFinalPathNameByHandleW,
   * which always returns an extended-length path starting with \\?\.
   * From there, take the prefix up to and including the backslash after
   * the volume component.
   *
   *   \\?\C:\path\to\file        → \\?\C:\
   *   \\?\Volume{GUID}\path      → \\?\Volume{GUID}\
   *   \\?\UNC\server\share\path  → \\?\UNC\server\share\
   */
static bool win_get_volume_root(HANDLE hFile, wchar_t* out, DWORD out_cap) {
    /* Query the final path. Grow the buffer if the initial guess is
     * too small. */
    DWORD cap = 512;
    wchar_t* path = NULL;

    for (;;) {
        wchar_t* new_path = (wchar_t*)realloc(path, cap * sizeof(wchar_t));
        if (new_path == NULL) {
            free(path);
            return false;
        }
        path = new_path;

        DWORD len = GetFinalPathNameByHandleW(hFile, path, cap, 0);
        if (len == 0) {
            free(path);
            return false;
        }
        if (len < cap) {
            /* Success; len is length excluding NUL. */
            break;
        }
        /* Need a bigger buffer. len is the required size including NUL. */
        cap = len + 1;
    }

    bool ok = false;
    out[0] = L'\0';

    if (wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0) {
        /*
         * UNC form: \\?\UNC\server\share\...
         * Volume root: \\?\UNC\server\share\ (include trailing slash)
         */
        const wchar_t* p = path + 8;
        int backslashes = 0;
        while (*p && backslashes < 2) {
            if (*p == L'\\') backslashes++;
            p++;
        }
        size_t prefix_len = (size_t)(p - path);
        if (prefix_len < out_cap) {
            wmemcpy(out, path, prefix_len);
            out[prefix_len] = L'\0';
            ok = true;
        }
    }
    else if (wcsncmp(path, L"\\\\?\\", 4) == 0) {
        /*
         * Drive or volume-GUID form: \\?\C:\... or \\?\Volume{...}\...
         * Volume root: prefix up to and including the next backslash.
         */
        const wchar_t* p = path + 4;
        while (*p && *p != L'\\') p++;
        if (*p == L'\\') {
            size_t prefix_len = (size_t)(p - path) + 1;  /* include slash */
            if (prefix_len < out_cap) {
                wmemcpy(out, path, prefix_len);
                out[prefix_len] = L'\0';
                ok = true;
            }
        }
    }

    free(path);
    return ok;
}

/**
 * @brief Get the cluster size of the volume containing the file.
 *
 * Returns 0 if the volume can't be determined or queried. Callers
 * should fall back to 4096 in that case.
 */
static DWORD win_get_cluster_size(HANDLE hFile) {
    wchar_t volume_root[MAX_PATH];
    if (!win_get_volume_root(hFile, volume_root, MAX_PATH)) {
        return 0;
    }

    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD free_clusters = 0;
    DWORD total_clusters = 0;

    if (!GetDiskFreeSpaceW(volume_root,
        &sectors_per_cluster,
        &bytes_per_sector,
        &free_clusters,
        &total_clusters)) {
        return 0;
    }
    return sectors_per_cluster * bytes_per_sector;
}

/**
 * @brief Count clusters actually allocated to a file.
 *
 * Uses FSCTL_GET_RETRIEVAL_POINTERS. Walks the extent list, summing
 * VCN spans where the extent's Lcn is not -1 (i.e. not a hole).
 *
 * The RETRIEVAL_POINTERS_BUFFER structure in winioctl.h is:
 *
 *     typedef struct {
 *         DWORD ExtentCount;
 *         LARGE_INTEGER StartingVcn;
 *         struct {
 *             LARGE_INTEGER NextVcn;
 *             LARGE_INTEGER Lcn;
 *         } Extents[1];
 *     } RETRIEVAL_POINTERS_BUFFER;
 *
 * There is no named typedef for the extent element type, so we use
 * sizeof(rp->Extents[0]) rather than a struct type name.
 *
 * Returns 0 if the ioctl is not supported or fails unexpectedly.
 */
static uint64_t win_count_allocated_clusters(HANDLE hFile) {
    STARTING_VCN_INPUT_BUFFER input;
    input.StartingVcn.QuadPart = 0;

    const DWORD initial_buf_size = 8192;
    DWORD buf_size = initial_buf_size;
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (buf == NULL) return 0;

    uint64_t total_clusters = 0;
    uint64_t resume_vcn = 0;

    for (;;) {
        DWORD bytes_returned = 0;
        BOOL ok = DeviceIoControl(hFile,
            FSCTL_GET_RETRIEVAL_POINTERS,
            &input, sizeof(input),
            buf, buf_size,
            &bytes_returned, NULL);

        DWORD last_error = ok ? 0 : GetLastError();
        if (!ok && last_error != ERROR_MORE_DATA) {
            /* Real error (not "buffer too small"). Give up. */
            free(buf);
            return total_clusters;
        }

        /*
         * Compute how many extents actually fit in bytes_returned. The
         * header occupies offsetof(RETRIEVAL_POINTERS_BUFFER, Extents)
         * bytes; each extent element is sizeof(rp->Extents[0]).
         */
        size_t header_size = offsetof(RETRIEVAL_POINTERS_BUFFER, Extents);
        if (bytes_returned < header_size) {
            /* No usable data. If this was the first call, we're done. */
            break;
        }

        RETRIEVAL_POINTERS_BUFFER* rp = (RETRIEVAL_POINTERS_BUFFER*)buf;

        size_t avail_bytes = (size_t)bytes_returned - header_size;
        DWORD fitting = (DWORD)(avail_bytes / sizeof(rp->Extents[0]));

        DWORD count = rp->ExtentCount;
        if (count > fitting) count = fitting;

        uint64_t prev_vcn = rp->StartingVcn.QuadPart;
        for (DWORD i = 0; i < count; i++) {
            uint64_t next_vcn = rp->Extents[i].NextVcn.QuadPart;
            if (next_vcn == 0xFFFFFFFFFFFFFFFFULL) {
                /* Sentinel: end of extent list. */
                free(buf);
                return total_clusters;
            }

            int64_t lcn = rp->Extents[i].Lcn.QuadPart;

            /*
             * The Lcn describes the range [prev_vcn, next_vcn).
             * Lcn == -1 marks a hole. Anything else is real clusters.
             */
            if (lcn != -1) {
                total_clusters += next_vcn - prev_vcn;
            }

            prev_vcn = next_vcn;
        }

        if (ok) {
            /* Success and we've consumed all extents. */
            break;
        }

        /*
         * ERROR_MORE_DATA path. We consumed `count` extents; resume
         * from the last VCN. Guard against no-progress to avoid an
         * infinite loop.
         */
        if (prev_vcn == resume_vcn) break;
        resume_vcn = prev_vcn;
        input.StartingVcn.QuadPart = prev_vcn;

        /* Grow the buffer if we filled it completely. */
        if (count == fitting) {
            buf_size *= 2;
            if (buf_size > 4 * 1024 * 1024) break;
            uint8_t* nb = (uint8_t*)realloc(buf, buf_size);
            if (nb == NULL) break;
            buf = nb;
        }
    }

    free(buf);
    return total_clusters;
}

#endif  /* _WIN32 */

_m_result_t _m_allocated_size(_m_handle_t h) {
    _m_clear_error();

    if (h == _M_HANDLE_INVALID) {
        _M_RETURN_ERR(_M_ERR_INVALID_ARG,
            "_m_allocated_size: invalid handle");
    }

#ifdef _WIN32
    HANDLE fh = (HANDLE)(intptr_t)h;

    DWORD cluster_size = win_get_cluster_size(fh);
    if (cluster_size == 0) {
        cluster_size = 4096;   /* NTFS default fallback */
    }

    uint64_t clusters = win_count_allocated_clusters(fh);

    if (clusters == 0) {
        /*
         * Either the ioctl isn't supported (rare) or the file is truly
         * empty. Fall back to logical size so the caller gets a value
         * rather than a hard failure.
         */
        LARGE_INTEGER li;
        if (!GetFileSizeEx(fh, &li)) {
            DWORD err = GetLastError();
            _M_RETURN_ERR(_M_ERR_IO,
                "_m_allocated_size: GetFileSizeEx failed "
                "(error=%lu)", (unsigned long)err);
        }
        return _m_ok((int64_t)li.QuadPart);
    }

    return _m_ok((int64_t)(clusters * (uint64_t)cluster_size));

#else  /* POSIX */
    struct stat st;
    if (fstat((int)(h & 0xFFFFFFFF), &st) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_allocated_size: fstat failed (errno=%d)", err);
    }
    return _m_ok((int64_t)st.st_blocks * 512);
#endif
}