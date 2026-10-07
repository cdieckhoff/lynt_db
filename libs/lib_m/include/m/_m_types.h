/**
 * @file _m_types.h
 * @brief Core type definitions for the _m universal OS layer.
 *
 * This header defines the vocabulary that every _m_* function shares:
 * the universal handle type, result structs, error codes, and open flags.
 *
 * DESIGN PRINCIPLES
 * -----------------
 * 1. Every _m_* function returns a struct, never a bare value. This mirrors
 *    Go's (value, error) idiom and Rust's Result<T, E>.
 *
 * 2. The handle type is int64_t. On Windows it holds a HANDLE (pointer-sized,
 *    8 bytes). On POSIX it holds an int fd in the low 32 bits with the upper
 *    32 bits zero. Callers never inspect the value; they pass it back to
 *    other _m_* functions.
 *
 * 3. The invalid-handle sentinel is (int64_t)-1, which matches both
 *    INVALID_HANDLE_VALUE on Windows and -1 from open() on POSIX.
 *
 * 4. Sizes, offsets, and byte counts are int64_t. Never size_t. This keeps
 *    the ABI identical across 32-bit and 64-bit builds.
 *
 * 5. Platform types (HANDLE, DWORD, OVERLAPPED, off_t, LARGE_INTEGER) NEVER
 *    appear in any public signature. They are confined to function bodies.
 *
 * 6. No public header includes <windows.h>. If you find yourself wanting to,
 *    the type is leaking and should be wrapped.
 */

#ifndef M__M_TYPES_H
#define M__M_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* ====================================================================
     * SECTION: Universal Handle
     * ==================================================================== */

     /**
      * @brief Universal file handle.
      *
      * On Windows: holds a HANDLE, stored as (int64_t)(intptr_t)h.
      * On POSIX:   holds an int fd, stored as (int64_t)fd (zero-extended).
      *
      * Treat as opaque. Obtain from _m_open(), release with _m_close().
      */
    typedef int64_t _m_handle_t;

    /**
     * @brief The invalid-handle sentinel.
     *
     * Chosen so that the same constant works on both platforms:
     *   Windows: INVALID_HANDLE_VALUE is (HANDLE)-1, which round-trips to -1.
     *   POSIX:   open() returns -1 on failure, which widens to -1.
     */
#define _M_HANDLE_INVALID ((_m_handle_t)-1)

     /**
      * @brief Size / offset / byte-count type.
      *
      * Signed so that -1 can be used as an error marker internally, and so that
      * negative results from pread/pwrite propagate cleanly. Always 64-bit.
      */
    typedef int64_t _m_size_t;

    /* ====================================================================
     * SECTION: Error Codes
     * ==================================================================== */

     /**
      * @brief Error codes returned in _m_result_t::code.
      *
      * 0 (_M_OK) means success. Every other value is a failure. These are NOT
      * errno values; they are our own namespace so we don't collide with the
      * C library's <errno.h>.
      *
      * When mapping a platform error to one of these, prefer the closest
      * semantic match rather than 1:1 translation. The caller cares whether
      * it was a permissions problem vs a missing file, not whether errno was
      * EACCES vs EPERM.
      */
    typedef enum {
        _M_OK = 0,   /**< Success. */
        _M_ERR_INVALID_ARG = 1,   /**< NULL path, zero length, bad flag, etc. */
        _M_ERR_NOT_FOUND = 2,   /**< File does not exist (open without O_CREAT). */
        _M_ERR_PERMISSION = 3,   /**< Access denied by OS or filesystem. */
        _M_ERR_IO = 4,   /**< Read/write/seek/truncate failure. */
        _M_ERR_NOMEM = 5,   /**< Allocation failure. */
        _M_ERR_OUT_OF_RANGE = 6,   /**< Offset beyond file/region bounds. */
        _M_ERR_NOT_SUPPORTED = 7,   /**< Operation not implemented on this platform/FS. */
        _M_ERR_ALREADY_EXISTS = 8,   /**< O_CREAT | O_EXCL and the file exists. */
        _M_ERR_MM_FAILED = 9,   /**< mmap / MapViewOfFile failure. */
        _M_ERR_UNKNOWN = 99,  /**< Uncategorized. Usually accompanied by a message. */
    } _m_err_t;

    /* ====================================================================
     * SECTION: Result Structs
     * ==================================================================== */

     /**
      * @brief Result of an operation that returns an integer (handle, size, byte count).
      *
      * On success: code == _M_OK, value holds the result.
      * On failure: code != _M_OK, value is 0. Callers MUST check code first.
      *
      * Example:
      *     _m_result_t r = _m_open("data.lynt", _M_O_RDWR | _M_O_CREAT, 0644);
      *     if (r.code != _M_OK) {
      *         fprintf(stderr, "%s\n", _m_last_error());
      *         return -1;
      *     }
      *     _m_handle_t h = (_m_handle_t)r.value;
      */
    typedef struct {
        int32_t  code;    /**< _M_OK on success, otherwise an _m_err_t value. */
        int32_t  _pad;    /**< Explicit padding: keeps the struct 16 bytes and
                               makes the layout identical on all ABIs. */
        int64_t  value;   /**< Payload on success; 0 on failure. */
    } _m_result_t;

    /**
     * @brief Result of an operation that returns a pointer (mmap, aligned_alloc).
     *
     * On success: code == _M_OK, ptr is non-NULL.
     * On failure: code != _M_OK, ptr is NULL.
     */
    typedef struct {
        int32_t  code;    /**< _M_OK on success, otherwise an _m_err_t value. */
        int32_t  _pad;    /**< Explicit padding (see _m_result_t). */
        void* ptr;     /**< Payload on success; NULL on failure. */
    } _m_ptr_result_t;

    /* ====================================================================
 * SECTION: Mapping Handles
 * ==================================================================== */

 /**
  * @brief Opaque handle representing a live memory mapping.
  *
  * WHY THIS EXISTS
  * ---------------
  * On POSIX, mmap and munmap take exactly the same arguments (addr, len),
  * and the returned pointer IS the mapping's base address. The caller can
  * pass either back to munmap and it works.
  *
  * On Windows, this breaks down in two ways:
  *
  *   1. MapViewOfFileEx requires the file offset to be a multiple of
  *      dwAllocationGranularity (64 KiB). If the caller wants to map
  *      at offset 16384, we must actually map at offset 0 with a larger
  *      length, and return a pointer that is offset into the mapping.
  *      The pointer the caller sees is NOT the base address that
  *      UnmapViewOfFile needs.
  *
  *   2. Windows has no analog of munmap taking both addr and len.
  *      UnmapViewOfFile takes only the base address.
  *
  * To present POSIX semantics uniformly, _m_map returns an opaque
  * _m_mapping_t* instead of a raw pointer. The caller extracts the usable
  * address with _m_mapping_addr() and releases the mapping with
  * _m_unmap(). The alignment bookkeeping lives inside the struct and is
  * never exposed.
  *
  * CALLER PATTERN
  * --------------
  *     _m_ptr_result_t mp = _m_map(NULL, 16384,
  *                                 _M_PROT_READ | _M_PROT_WRITE,
  *                                 _M_MAP_SHARED, h, 16384);
  *     if (mp.code != _M_OK) { ... }
  *
  *     uint8_t* p = _m_mapping_addr((_m_mapping_t*)mp.ptr);
  *     // p points to file offset 16384, usable for exactly 16384 bytes
  *
  *     _m_unmap((_m_mapping_t*)mp.ptr);
  *
  * The struct definition is in _m_map.h, not here, because callers should
  * never need to inspect its fields.
  */
    typedef struct _m_mapping _m_mapping_t;

    /**
     * @brief Convenience constructors. Use these inside _m_* implementations
     *        instead of writing out the struct literals by hand.
     */
    static inline _m_result_t _m_ok(int64_t v) {
        _m_result_t r = { _M_OK, 0, v };
        return r;
    }
    static inline _m_result_t _m_err(int32_t code) {
        _m_result_t r = { code, 0, 0 };
        return r;
    }
    static inline _m_ptr_result_t _m_ok_ptr(void* p) {
        _m_ptr_result_t r = { _M_OK, 0, p };
        return r;
    }
    static inline _m_ptr_result_t _m_err_ptr(int32_t code) {
        _m_ptr_result_t r = { code, 0, NULL };
        return r;
    }

    /* ====================================================================
     * SECTION: Open Flags (POSIX values)
     * ==================================================================== */

     /*
      * These match the POSIX values on Linux and macOS, so on those platforms
      * they can be passed straight to open(2) with no translation. On Windows,
      * _m_open() parses these bits and translates to CreateFileW parameters.
      *
      * We deliberately do NOT define _M_O_EXCL, _M_O_NOFOLLOW, etc. Add them
      * as needed, and remember to translate them in _m_open() on Windows too.
      */

#define _M_O_RDONLY   0x0000   /**< Read-only. Value 0, as in POSIX. */
#define _M_O_WRONLY   0x0001   /**< Write-only. */
#define _M_O_RDWR     0x0002   /**< Read and write. */
#define _M_O_CREAT    0x0040   /**< Create if not exists. */
#define _M_O_EXCL     0x0080   /**< With _M_O_CREAT: fail if exists. */
#define _M_O_TRUNC    0x0200   /**< Truncate to zero on open. */
#define _M_O_APPEND   0x0400   /**< Writes always go to end of file. */

      /* ====================================================================
       * SECTION: mmap Protection and Flags
       * ==================================================================== */

#define _M_PROT_NONE   0x0     /**< No access (rarely useful). */
#define _M_PROT_READ   0x1     /**< Pages may be read. */
#define _M_PROT_WRITE  0x2     /**< Pages may be written. */
#define _M_PROT_EXEC   0x4     /**< Pages may be executed (not used yet). */

#define _M_MAP_SHARED  0x1     /**< Writes go to the file (default for DB). */
#define _M_MAP_PRIVATE 0x2     /**< Copy-on-write; writes do not affect file. */

       /* ====================================================================
        * SECTION: Seek Whence
        * ==================================================================== */

        /*
         * These values match both POSIX (SEEK_SET etc.) AND Windows
         * (FILE_BEGIN etc.), so no translation is needed on any platform.
         */

#define _M_SEEK_SET    0       /**< From start of file. */
#define _M_SEEK_CUR    1       /**< From current position. */
#define _M_SEEK_END    2       /**< From end of file. */

#ifdef __cplusplus
}
#endif

#endif /* M__M_TYPES_H */