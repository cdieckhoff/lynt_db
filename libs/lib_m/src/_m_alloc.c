/**
 * @file _m_alloc.c
 * @brief Aligned memory allocation.
 *
 * POSIX:   posix_memalign(&p, alignment, size). Returns 0 on success and
 *          sets errno on failure; we translate to _M_ERR_NOMEM.
 * Windows: _aligned_malloc(size, alignment). Returns NULL on failure.
 *
 * Alignment rules (enforced by the underlying APIs):
 *   - alignment must be a power of two
 *   - alignment must be a multiple of sizeof(void*)
 *
 * We do not validate these ourselves; the OS will reject bad inputs and
 * we'll return a generic error. If you want prettier messages, add checks.
 */

#include "m/_m.h"

#include <stdlib.h>

#ifdef _WIN32
#  include <malloc.h>
#else
#  include <errno.h>
#endif

_m_ptr_result_t _m_alloc(_m_size_t alignment, _m_size_t size) {
    _m_clear_error();

    if (alignment <= 0 || size <= 0) {
        _M_RETURN_ERR_PTR(_M_ERR_INVALID_ARG,
            "_m_alloc: alignment and size must be > 0");
    }

#ifdef _WIN32
    void* p = _aligned_malloc((size_t)size, (size_t)alignment);
    if (p == NULL) {
        _M_RETURN_ERR_PTR(_M_ERR_NOMEM,
            "_m_alloc: _aligned_malloc(%lld, %lld) failed",
            (long long)size, (long long)alignment);
    }
    return _m_ok_ptr(p);
#else
    void* p = NULL;
    int rc = posix_memalign(&p, (size_t)alignment, (size_t)size);
    if (rc != 0) {
        /* posix_memalign returns EINVAL or ENOMEM. */
        int code = (rc == EINVAL) ? _M_ERR_INVALID_ARG : _M_ERR_NOMEM;
        _M_RETURN_ERR_PTR(code,
            "_m_alloc: posix_memalign(%lld, %lld) failed (errno=%d)",
            (long long)size, (long long)alignment, rc);
    }
    return _m_ok_ptr(p);
#endif
}

void _m_free(void* ptr) {
    if (ptr == NULL) return;
#ifdef _WIN32
    /*
     * Must use _aligned_free for memory from _aligned_malloc. Regular
     * free() will crash on Windows because the allocator stores bookkeeping
     * info in the bytes before the returned pointer.
     */
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}