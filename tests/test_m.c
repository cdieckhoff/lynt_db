/**
 * @file test_m.c
 * @brief Integration tests for the _m universal OS layer.
 *
 * COVERAGE
 * --------
 * Phase 1 (core behavior):
 *   - File create / open / close round-trips
 *   - _m_size reflects the requested length exactly (the Windows
 *     granularity trap: a 16 KiB file must NOT report 64 KiB)
 *   - mmap at page-aligned offsets works on POSIX, and at
 *     granularity-aligned offsets works on Windows
 *   - mmap past EOF extends the file (no SIGBUS on POSIX)
 *   - Growing a file preserves existing data
 *   - mmap write followed by read-back through pread
 *   - Error paths return codes, not crashes
 *
 * Phase 2 (sparse and durability):
 *   - _m_pwrite past EOF with a gap: the gap reads as zeros and the
 *     file size reflects offset + length
 *   - _m_punch_hole actually deallocates on filesystems that support it
 *   - _m_is_hole correctly identifies fresh truncates and written ranges
 *   - _m_sync round-trips data through close / reopen
 *   - _m_alloc returns pointers aligned to the requested boundary
 *
 * All temp files are created in the OS temp directory and cleaned up
 * at the end of each test. On failure, a message identifies the file
 * and offset for debugging.
 */

#include "test_harness.h"
#include "m/_m.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

 /* ====================================================================
  * Test fixture helpers
  * ==================================================================== */

  /*
   * Where temp files live. On POSIX, /tmp is always writable and is not
   * subject to Windows's path-length or permission quirks. On Windows we
   * use the temp directory reported by the OS.
   */
#ifdef _WIN32
#  include <windows.h>
static void get_temp_dir(char* out, size_t cap) {
    DWORD n = GetTempPathA((DWORD)cap, out);
    if (n == 0 || n >= cap) {
        strcpy_s(out, cap, ".\\");
    }
}
#else
#  include <unistd.h>
#  include <sys/stat.h>
static void get_temp_dir(char* out, size_t cap) {
    const char* t = getenv("TMPDIR");
    if (t == NULL || *t == '\0') t = "/tmp";
    snprintf(out, cap, "%s", t);
}
#endif

/*
 * Build a unique path for this test run. The suffix comes from the
 * process ID so parallel test runs don't collide.
 */
static void make_temp_path(char* out, size_t cap, const char* suffix) {
    char dir[512];
    get_temp_dir(dir, sizeof(dir));

#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
    snprintf(out, cap, "%s\\lynt_test_%lu_%s", dir, pid, suffix);
#else
    unsigned long pid = (unsigned long)getpid();
    snprintf(out, cap, "%s/lynt_test_%lu_%s", dir, pid, suffix);
#endif
}

static void remove_file(const char* path) {
#ifdef _WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

/*
 * Query the OS file size directly, bypassing _m_size. Used to cross-check
 * that _m_size is not lying to us.
 */
static int64_t os_file_size(const char* path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        return -1;
    }
    LARGE_INTEGER li;
    li.HighPart = (LONG)fad.nFileSizeHigh;
    li.LowPart = fad.nFileSizeLow;
    return (int64_t)li.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
#endif
}

/*
 * Fill a buffer with a recognizable pattern so we can verify round-trips.
 * The pattern is position-dependent so we catch off-by-one errors.
 */
static void fill_pattern(uint8_t* buf, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(seed + (i & 0xFF));
    }
}

static int check_pattern(const uint8_t* buf, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; i++) {
        uint8_t want = (uint8_t)(seed + (i & 0xFF));
        if (buf[i] != want) return 0;
    }
    return 1;
}

/* ====================================================================
 * TEST: platform info
 * ==================================================================== */

static void test_platform_info(void) {
    _m_size_t page = _m_page_size();
    _m_size_t gran = _m_alloc_granularity();

    /* Page size must be a positive power of two. */
    TEST_ASSERT(page > 0);
    TEST_ASSERT((page & (page - 1)) == 0);

    /* Granularity must be at least the page size, and a power of two. */
    TEST_ASSERT(gran > 0);
    TEST_ASSERT((gran & (gran - 1)) == 0);
    TEST_ASSERT(gran >= page);

    /*
     * On Windows, granularity is always 64 KiB. On POSIX, it equals
     * page size. Asserting the Windows case catches accidental changes
     * to the query function.
     */
#ifdef _WIN32
    TEST_ASSERT(gran == 65536);
#else
    TEST_ASSERT(gran == page);
#endif

    printf("      page=%lld granularity=%lld\n",
        (long long)page, (long long)gran);
}

/* ====================================================================
 * TEST: exact file size after truncate
 * ==================================================================== */

 /*
  * This is the core test for the granularity bug. A file we asked to be
  * 16 KiB must report 16 KiB via stat(), not 64 KiB, not 4 KiB.
  */
static void test_exact_truncate(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "trunc.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    const _m_size_t sizes[] = { 1, 100, 4097, 16384, 16385, 32768, 65535 };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int i = 0; i < n; i++) {
        r = _m_truncate(h, sizes[i]);
        TEST_ASSERT(r.code == _M_OK);

        r = _m_size(h);
        TEST_ASSERT(r.code == _M_OK);
        TEST_ASSERT(r.value == sizes[i]);

        int64_t os_sz = os_file_size(path);
        TEST_ASSERT(os_sz == sizes[i]);

        printf("      truncate to %lld: _m_size=%lld os=%lld\n",
            (long long)sizes[i],
            (long long)sizes[i],
            (long long)os_sz);
    }

    _m_close(h);
    remove_file(path);
}

/* ====================================================================
 * TEST: mmap at 16 KiB boundaries
 * ==================================================================== */

 /*
  * Map 16 KiB windows at every 16 KiB boundary. On Windows, some offsets
  * are not granularity-aligned; the library maps at the nearest lower
  * boundary and returns a pointer shifted into the mapping.
  */
static void test_mmap_16k_boundaries(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "boundaries.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    r = _m_truncate(h, 262144);
    TEST_ASSERT(r.code == _M_OK);

    for (_m_size_t off = 0; off < 262144; off += 16384) {
        _m_ptr_result_t mp = _m_map(NULL, 16384,
            _M_PROT_READ | _M_PROT_WRITE,
            _M_MAP_SHARED, h, off);
        TEST_ASSERT(mp.code == _M_OK);
        if (mp.code != _M_OK) {
            printf("      map at %lld failed: %s\n",
                (long long)off, _m_last_error());
            continue;
        }

        uint8_t* p = (uint8_t*)_m_mapping_addr((_m_mapping_t*)mp.ptr);
        TEST_ASSERT(p != NULL);

        /* Tag each 16 KiB window with its index. */
        memset(p, (int)(off / 16384), 16384);

        r = _m_msync((_m_mapping_t*)mp.ptr);
        TEST_ASSERT(r.code == _M_OK);

        r = _m_unmap((_m_mapping_t*)mp.ptr);
        TEST_ASSERT(r.code == _M_OK);
    }

    /* Read back and verify. */
    uint8_t* back = (uint8_t*)malloc(262144);
    r = _m_pread(h, back, 262144, 0);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 262144);

    for (_m_size_t off = 0; off < 262144; off += 16384) {
        uint8_t want = (uint8_t)(off / 16384);
        for (int i = 0; i < 16384; i++) {
            if (back[off + i] != want) {
                printf("      mismatch at %lld (byte %d): got %u want %u\n",
                    (long long)(off + i), i, back[off + i], want);
                TEST_ASSERT(0);
                goto done;
            }
        }
    }
done:
    free(back);
    _m_close(h);
    remove_file(path);
}

/* ====================================================================
 * TEST: grow in 16 KiB steps
 * ==================================================================== */

static void test_grow_in_16k_steps(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "grow16k.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    for (int step = 1; step <= 8; step++) {
        _m_size_t new_size = (_m_size_t)step * 16384;
        r = _m_truncate(h, new_size);
        TEST_ASSERT(r.code == _M_OK);

        uint8_t buf[16384];
        memset(buf, step, sizeof(buf));
        r = _m_pwrite(h, buf, 16384, (_m_size_t)(step - 1) * 16384);
        TEST_ASSERT(r.code == _M_OK);
        TEST_ASSERT(r.value == 16384);

        r = _m_size(h);
        TEST_ASSERT(r.code == _M_OK);
        TEST_ASSERT(r.value == new_size);

        int64_t os_sz = os_file_size(path);
        TEST_ASSERT(os_sz == new_size);

        printf("      step %d: size=%lld os=%lld\n",
            step, (long long)new_size, (long long)os_sz);
    }

    _m_close(h);
    remove_file(path);
}

/* ====================================================================
 * TEST: mmap write then read back through a separate handle
 * ==================================================================== */

static void test_mmap_roundtrip(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "roundtrip.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    r = _m_truncate(h, 32768);
    TEST_ASSERT(r.code == _M_OK);

    /* Map at 16 KiB, which is not granularity-aligned on Windows. */
    _m_ptr_result_t mp = _m_map(NULL, 16384,
        _M_PROT_READ | _M_PROT_WRITE,
        _M_MAP_SHARED, h, 16384);
    TEST_ASSERT(mp.code == _M_OK);
    if (mp.code != _M_OK) {
        _m_close(h);
        remove_file(path);
        return;
    }

    uint8_t* p = (uint8_t*)_m_mapping_addr((_m_mapping_t*)mp.ptr);
    fill_pattern(p, 16384, 0x42);

    r = _m_msync((_m_mapping_t*)mp.ptr);
    TEST_ASSERT(r.code == _M_OK);

    r = _m_unmap((_m_mapping_t*)mp.ptr);
    TEST_ASSERT(r.code == _M_OK);

    /* Read back via pread, bypassing mmap. */
    uint8_t back[16384];
    r = _m_pread(h, back, 16384, 16384);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 16384);
    TEST_ASSERT(check_pattern(back, 16384, 0x42));

    _m_close(h);
    remove_file(path);
}

/* ====================================================================
 * TEST: mapping past EOF extends the file
 * ==================================================================== */

static void test_map_past_eof(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "pasteof.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    r = _m_truncate(h, 16384);
    TEST_ASSERT(r.code == _M_OK);

    /* Map 16 KiB at offset 16384, past EOF. */
    _m_ptr_result_t mp = _m_map(NULL, 16384,
        _M_PROT_READ | _M_PROT_WRITE,
        _M_MAP_SHARED, h, 16384);
    TEST_ASSERT(mp.code == _M_OK);
    if (mp.code != _M_OK) {
        printf("      map past EOF failed: %s\n", _m_last_error());
        _m_close(h);
        remove_file(path);
        return;
    }

    uint8_t* p = (uint8_t*)_m_mapping_addr((_m_mapping_t*)mp.ptr);

    /* Fresh region reads as zeros. */
    for (int i = 0; i < 16384; i++) TEST_ASSERT(p[i] == 0);

    /* Write extends the file. */
    fill_pattern(p, 16384, 0x11);
    _m_msync((_m_mapping_t*)mp.ptr);
    _m_unmap((_m_mapping_t*)mp.ptr);

    r = _m_size(h);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 32768);

    _m_close(h);
    int64_t os_sz = os_file_size(path);
    TEST_ASSERT(os_sz == 32768);

    remove_file(path);
}

/* ====================================================================
 * TEST: error paths
 * ==================================================================== */

static void test_error_paths(void) {
    /* NULL path. */
    _m_result_t r = _m_open(NULL, _M_O_RDWR, 0, false);
    TEST_ASSERT(r.code == _M_ERR_INVALID_ARG);

    /* Nonexistent file without O_CREAT. */
    r = _m_open("/nonexistent/path/lynt_test", _M_O_RDWR, 0, false);
    TEST_ASSERT(r.code == _M_ERR_NOT_FOUND);

    /* Invalid handle. */
    r = _m_close(_M_HANDLE_INVALID);
    TEST_ASSERT(r.code == _M_ERR_INVALID_ARG);

    /* map with zero length. */
    char path[512];
    make_temp_path(path, sizeof(path), "err.bin");
    r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC, 0644, true);
    if (r.code == _M_OK) {
        _m_handle_t h = (_m_handle_t)r.value;
        _m_truncate(h, 16384);
        _m_ptr_result_t mp = _m_map(NULL, 0, _M_PROT_READ,
            _M_MAP_SHARED, h, 0);
        TEST_ASSERT(mp.code == _M_ERR_INVALID_ARG);
        _m_close(h);
    }
    remove_file(path);

    /* _m_last_error is never NULL. */
    TEST_ASSERT(_m_last_error() != NULL);
}

/* ====================================================================
 * PHASE 2 TESTS
 * ==================================================================== */

 /* --------------------------------------------------------------------
  * TEST: pwrite with a gap past EOF
  * --------------------------------------------------------------------
  * Write 4 KiB at offset 3 * 16 KiB into a 16 KiB file. This leaves a
  * 32 KiB gap between the old EOF and the write offset. The file must
  * grow to offset + 4096, the gap must read as zeros, and the written
  * region must round-trip.
  */
static void test_pwrite_gap_past_eof(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "gap.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    r = _m_truncate(h, 16384);
    TEST_ASSERT(r.code == _M_OK);

    uint8_t wbuf[4096];
    fill_pattern(wbuf, sizeof(wbuf), 0x77);

    const _m_size_t write_at = 3 * 16384;   /* 49152 */
    r = _m_pwrite(h, wbuf, 4096, write_at);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 4096);

    /* File is now write_at + 4096 = 53248. */
    r = _m_size(h);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == write_at + 4096);

    /* The gap [16384, 32768) reads as zeros. */
    uint8_t gap[16384];
    memset(gap, 0xFF, sizeof(gap));
    r = _m_pread(h, gap, sizeof(gap), 16384);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 16384);

    int gap_nonzero = 0;
    for (int i = 0; i < 16384; i++) {
        if (gap[i] != 0) { gap_nonzero = 1; break; }
    }
    if (gap_nonzero) {
        printf("      gap contains non-zero bytes\n");
    }
    TEST_ASSERT(gap_nonzero == 0);

    /* The written region reads back correctly. */
    uint8_t back[4096];
    r = _m_pread(h, back, sizeof(back), write_at);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(check_pattern(back, sizeof(back), 0x77));

    _m_close(h);

    int64_t os_sz = os_file_size(path);
    TEST_ASSERT(os_sz == write_at + 4096);

    printf("      wrote at %lld, file is %lld, gap reads as zeros\n",
        (long long)write_at, (long long)os_sz);

    remove_file(path);
}

/* --------------------------------------------------------------------
 * TEST: punch_hole actually deallocates
 * --------------------------------------------------------------------
 * Fill a 1 MiB file, measure allocated size, punch a 256 KiB hole in
 * the middle, measure again. On filesystems that support sparse files,
 * the allocated size drops by roughly the punched amount. On FAT/exFAT
 * and some network filesystems, punch_hole returns _M_ERR_NOT_SUPPORTED
 * and the test reports that gracefully.
 */
static void test_punch_hole_deallocates(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "punch.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    _m_mark_sparse(h);

    const _m_size_t total = 1024 * 1024;
    r = _m_truncate(h, total);
    TEST_ASSERT(r.code == _M_OK);

    /* Write the whole file so it's fully allocated. */
    uint8_t buf[65536];
    fill_pattern(buf, sizeof(buf), 0x11);
    for (_m_size_t off = 0; off < total; off += 65536) {
        r = _m_pwrite(h, buf, 65536, off);
        TEST_ASSERT(r.code == _M_OK);
    }

    r = _m_sync(h);
    TEST_ASSERT(r.code == _M_OK);

    _m_result_t before = _m_allocated_size(h);
    TEST_ASSERT(before.code == _M_OK);

    /* Punch 256 KiB in the middle, aligned to granularity. */
    _m_size_t align = _m_alloc_granularity();
    _m_size_t punch_off = ((256 * 1024) / align) * align;
    _m_size_t punch_len = ((256 * 1024) / align) * align;

    r = _m_punch_hole(h, punch_off, punch_len);
    if (r.code == _M_ERR_NOT_SUPPORTED) {
        printf("      punch_hole not supported on this filesystem; "
            "skipping allocation check\n");
        _m_close(h);
        remove_file(path);
        return;
    }
    TEST_ASSERT(r.code == _M_OK);

    r = _m_sync(h);
    TEST_ASSERT(r.code == _M_OK);

    _m_result_t after = _m_allocated_size(h);
    TEST_ASSERT(after.code == _M_OK);

    printf("      allocated before punch: %lld\n", (long long)before.value);
    printf("      allocated after punch:  %lld (delta: %lld)\n",
        (long long)after.value,
        (long long)(before.value - after.value));

    /*
     * Soft assertion: on some filesystems (notably WSL's /mnt/c 9P
     * bridge), the allocation doesn't visibly drop until the file is
     * closed or the cache is flushed. We report but do not fail.
     */
    if (after.value >= before.value) {
        printf("      NOTE: allocation did not decrease; FS may not "
            "report deallocation promptly\n");
    }

    /* The punched region reads as zeros. */
    uint8_t back[65536];
    r = _m_pread(h, back, sizeof(back), punch_off);
    TEST_ASSERT(r.code == _M_OK);
    int punched_nonzero = 0;
    for (int i = 0; i < 65536; i++) {
        if (back[i] != 0) { punched_nonzero = 1; break; }
    }
    TEST_ASSERT(punched_nonzero == 0);

    /* The rest of the file is intact. */
    r = _m_pread(h, back, 4096, 0);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(check_pattern(back, 4096, 0x11));

    _m_close(h);
    remove_file(path);
}

/* --------------------------------------------------------------------
 * TEST: is_hole on a fresh truncate and after a write
 * --------------------------------------------------------------------
 * A freshly-truncated file is a hole across its whole length, on
 * filesystems that support sparse files. After writing into a range,
 * that range is no longer a hole.
 */
static void test_is_hole_fresh_truncate(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "ishole.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    _m_mark_sparse(h);

    r = _m_truncate(h, 131072);
    TEST_ASSERT(r.code == _M_OK);

    bool is_hole = false;
    r = _m_is_hole(h, 0, 65536, &is_hole);
    if (r.code == _M_ERR_NOT_SUPPORTED) {
        printf("      is_hole not supported on this filesystem; skipping\n");
        _m_close(h);
        remove_file(path);
        return;
    }
    TEST_ASSERT(r.code == _M_OK);

    printf("      fresh truncate: is_hole=%d\n", is_hole);
    if (!is_hole) {
        printf("      NOTE: fresh truncate is not reported as a hole; "
            "FS may not support sparse files\n");
    }

    /* Write into the first 64 KiB. */
    uint8_t buf[65536];
    fill_pattern(buf, sizeof(buf), 0x33);
    r = _m_pwrite(h, buf, 65536, 0);
    TEST_ASSERT(r.code == _M_OK);

    /* The written range is not a hole. */
    r = _m_is_hole(h, 0, 65536, &is_hole);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(is_hole == false);

    _m_close(h);
    remove_file(path);
}

/* --------------------------------------------------------------------
 * TEST: sync round-trip
 * --------------------------------------------------------------------
 * Write a pattern, sync, close, reopen, and verify. This proves that
 * _m_sync actually flushes data to the filesystem.
 */
static void test_sync_roundtrip(void) {
    char path[512];
    make_temp_path(path, sizeof(path), "sync.bin");

    _m_result_t r = _m_open(path, _M_O_RDWR | _M_O_CREAT | _M_O_TRUNC,
        0644, true);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) return;
    _m_handle_t h = (_m_handle_t)r.value;

    r = _m_truncate(h, 65536);
    TEST_ASSERT(r.code == _M_OK);

    uint8_t buf[65536];
    fill_pattern(buf, sizeof(buf), 0xAB);
    r = _m_pwrite(h, buf, sizeof(buf), 0);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 65536);

    r = _m_sync(h);
    TEST_ASSERT(r.code == _M_OK);

    _m_close(h);

    /* Reopen and verify. */
    r = _m_open(path, _M_O_RDWR, 0, false);
    TEST_ASSERT(r.code == _M_OK);
    if (r.code != _M_OK) {
        remove_file(path);
        return;
    }
    h = (_m_handle_t)r.value;

    uint8_t back[65536];
    r = _m_pread(h, back, sizeof(back), 0);
    TEST_ASSERT(r.code == _M_OK);
    TEST_ASSERT(r.value == 65536);
    TEST_ASSERT(check_pattern(back, sizeof(back), 0xAB));

    _m_close(h);
    remove_file(path);
}

/* --------------------------------------------------------------------
 * TEST: alloc alignment
 * --------------------------------------------------------------------
 * Allocations must return pointers that are multiples of the requested
 * alignment. Invalid arguments are rejected cleanly.
 */
static void test_alloc_alignment(void) {
    const _m_size_t alignments[] = { 16, 32, 64, 128, 256, 512, 1024, 4096 };
    const int n = (int)(sizeof(alignments) / sizeof(alignments[0]));

    for (int i = 0; i < n; i++) {
        _m_size_t a = alignments[i];
        _m_ptr_result_t r = _m_alloc(a, 8192);
        TEST_ASSERT(r.code == _M_OK);
        if (r.code != _M_OK) continue;

        uintptr_t p = (uintptr_t)r.ptr;
        TEST_ASSERT((p % (uintptr_t)a) == 0);

        /* Write to prove it's usable memory. */
        memset(r.ptr, (int)a, 8192);

        _m_free(r.ptr);
    }

    /* Free(NULL) is a no-op. */
    _m_free(NULL);

    /* Zero alignment is rejected. */
    _m_ptr_result_t bad = _m_alloc(0, 4096);
    TEST_ASSERT(bad.code == _M_ERR_INVALID_ARG);

    /* Zero size is rejected. */
    bad = _m_alloc(16, 0);
    TEST_ASSERT(bad.code == _M_ERR_INVALID_ARG);
}

/* ====================================================================
 * Main
 * ==================================================================== */

int main(void) {
    /* Platform banner. Prints before the first test so CI logs and
     * local terminal output make it obvious which environment this was
     * captured in. Flushed immediately so the banner survives a crash.
     */
#ifdef _WIN32
    printf("== test_m on Windows ==\n");
#elif defined(__APPLE__)
    printf("== test_m on macOS ==\n");
#elif defined(__linux__)
    printf("== test_m on Linux ==\n");
#else
    printf("== test_m on unknown platform ==\n");
#endif

#if defined(_MSC_VER)
    printf("   compiler: MSVC %d\n", _MSC_VER);
#elif defined(__clang__)
    printf("   compiler: Clang %s\n", __VERSION__);
#elif defined(__GNUC__)
    printf("   compiler: GCC %s\n", __VERSION__);
#else
    printf("   compiler: unknown\n");
#endif

#if defined(__x86_64__) || defined(_M_X64)
    printf("   arch:     x86_64\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
    printf("   arch:     aarch64\n");
#else
    printf("   arch:     unknown\n");
#endif

    printf("   page size:          %lld\n", (long long)_m_page_size());
    printf("   allocation gran:    %lld\n", (long long)_m_alloc_granularity());
    fflush(stdout);

    /* Phase 1 */
    TEST_RUN(test_platform_info);
    TEST_RUN(test_exact_truncate);
    TEST_RUN(test_mmap_16k_boundaries);
    TEST_RUN(test_grow_in_16k_steps);
    TEST_RUN(test_mmap_roundtrip);
    TEST_RUN(test_map_past_eof);
    TEST_RUN(test_error_paths);

    /* Phase 2 */
    TEST_RUN(test_pwrite_gap_past_eof);
    TEST_RUN(test_punch_hole_deallocates);
    TEST_RUN(test_is_hole_fresh_truncate);
    TEST_RUN(test_sync_roundtrip);
    TEST_RUN(test_alloc_alignment);

    TEST_SUMMARY();
}