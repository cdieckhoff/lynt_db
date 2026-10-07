/**
 * @file _m_allocated_size.c
 * @brief Bytes actually consumed on disk.
 *
 * POSIX:   fstat gives st_blocks in 512-byte units. Multiply by 512.
 * Windows: FSCTL_GET_RETRIEVAL_POINTERS walks the extent list, summing
 *          spans where the extent's Lcn is not -1 (holes). The cluster
 *          size comes from GetDiskFreeSpaceW on the containing volume.
 */

#include "m/_m.h"

#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#else
#  include <sys/stat.h>
#  include <errno.h>
#endif

#ifdef _WIN32

static bool win_get_volume_root(HANDLE hFile, wchar_t* out, DWORD out_cap) {
    DWORD cap = 512;
    wchar_t* path = NULL;

    for (;;) {
        wchar_t* new_path = (wchar_t*)realloc(path, cap * sizeof(wchar_t));
        if (new_path == NULL) { free(path); return false; }
        path = new_path;

        DWORD len = GetFinalPathNameByHandleW(hFile, path, cap, 0);
        if (len == 0) { free(path); return false; }
        if (len < cap) break;
        cap = len + 1;
    }

    bool ok = false;
    out[0] = L'\0';

    if (wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0) {
        const wchar_t* p = path + 8;
        int bs = 0;
        while (*p && bs < 2) { if (*p == L'\\') bs++; p++; }
        size_t n = (size_t)(p - path);
        if (n < out_cap) { wmemcpy(out, path, n); out[n] = L'\0'; ok = true; }
    }
    else if (wcsncmp(path, L"\\\\?\\", 4) == 0) {
        const wchar_t* p = path + 4;
        while (*p && *p != L'\\') p++;
        if (*p == L'\\') {
            size_t n = (size_t)(p - path) + 1;
            if (n < out_cap) { wmemcpy(out, path, n); out[n] = L'\0'; ok = true; }
        }
    }

    free(path);
    return ok;
}

static DWORD win_get_cluster_size(HANDLE hFile) {
    wchar_t root[MAX_PATH];
    if (!win_get_volume_root(hFile, root, MAX_PATH)) return 0;

    DWORD spc = 0, bps = 0, fc = 0, tc = 0;
    if (!GetDiskFreeSpaceW(root, &spc, &bps, &fc, &tc)) return 0;
    return spc * bps;
}

static uint64_t win_count_allocated_clusters(HANDLE hFile) {
    STARTING_VCN_INPUT_BUFFER input;
    input.StartingVcn.QuadPart = 0;

    DWORD buf_size = 8192;
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (buf == NULL) return 0;

    uint64_t total = 0;
    uint64_t resume_vcn = 0;

    for (;;) {
        DWORD bytes_returned = 0;
        BOOL ok = DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS,
            &input, sizeof(input),
            buf, buf_size,
            &bytes_returned, NULL);

        DWORD last_error = ok ? 0 : GetLastError();
        if (!ok && last_error != ERROR_MORE_DATA) { free(buf); return total; }

        size_t header_size = offsetof(RETRIEVAL_POINTERS_BUFFER, Extents);
        if (bytes_returned < header_size) break;

        RETRIEVAL_POINTERS_BUFFER* rp = (RETRIEVAL_POINTERS_BUFFER*)buf;

        size_t avail = (size_t)bytes_returned - header_size;
        DWORD fitting = (DWORD)(avail / sizeof(rp->Extents[0]));

        DWORD count = rp->ExtentCount;
        if (count > fitting) count = fitting;

        uint64_t prev_vcn = rp->StartingVcn.QuadPart;
        for (DWORD i = 0; i < count; i++) {
            uint64_t next_vcn = rp->Extents[i].NextVcn.QuadPart;
            if (next_vcn == 0xFFFFFFFFFFFFFFFFULL) { free(buf); return total; }
            int64_t lcn = rp->Extents[i].Lcn.QuadPart;
            if (lcn != -1) total += next_vcn - prev_vcn;
            prev_vcn = next_vcn;
        }

        if (ok) break;

        if (prev_vcn == resume_vcn) break;
        resume_vcn = prev_vcn;
        input.StartingVcn.QuadPart = prev_vcn;

        if (count == fitting) {
            buf_size *= 2;
            if (buf_size > 4 * 1024 * 1024) break;
            uint8_t* nb = (uint8_t*)realloc(buf, buf_size);
            if (nb == NULL) break;
            buf = nb;
        }
    }

    free(buf);
    return total;
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
    if (cluster_size == 0) cluster_size = 4096;

    uint64_t clusters = win_count_allocated_clusters(fh);

    if (clusters == 0) {
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

#else
    struct stat st;
    if (fstat((int)(h & 0xFFFFFFFF), &st) != 0) {
        int err = errno;
        _M_RETURN_ERR(_M_ERR_IO,
            "_m_allocated_size: fstat failed (errno=%d)", err);
    }
    return _m_ok((int64_t)st.st_blocks * 512);
#endif
}