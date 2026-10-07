/**
 * @file _m_page.c
 * @brief Platform information: page size and allocation granularity.
 *
 * Both values are effectively constant for the life of the process, but
 * we call the OS query each time rather than caching. The cost is
 * negligible (a single syscall or a read from a kernel-maintained global)
 * and it avoids any static-initialization ordering issues.
 */

#include "m/_m.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

_m_size_t _m_page_size(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwPageSize;
#else
    long ps = sysconf(_SC_PAGESIZE);
    return (ps > 0) ? (int64_t)ps : 4096;
#endif
}

_m_size_t _m_alloc_granularity(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwAllocationGranularity;
#else
    /*
     * On POSIX, the granularity is the page size. mmap offset alignment
     * is the same as the page size.
     */
    return _m_page_size();
#endif
}