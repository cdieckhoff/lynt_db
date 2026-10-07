/**
 * @file _m_error.c
 * @brief Thread-local error message buffer.
 *
 * The buffer is 256 bytes: large enough for a formatted message with a
 * path, small enough to keep thread-local storage footprint modest. It is
 * truncated on overflow rather than dynamically allocated, because
 * allocating on the error path risks cascading failures.
 *
 * When _M_ENABLE_ERROR_MESSAGES is off, _m_set_error still exists as a
 * real function so that call sites remain uniform. It does nothing.
 * _m_last_error returns "".
 */

#include "m/_m_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_M_ENABLE_ERROR_MESSAGES) && _M_ENABLE_ERROR_MESSAGES

 /*
  * _Thread_local is C11. Supported by GCC, Clang, and MSVC 2019+.
  * On older MSVC it would need __declspec(thread), but we don't
  * support those.
  */
static _Thread_local char g_errbuf[256];
static _Thread_local int  g_has_error = 0;

void _m_set_error(int32_t code, const char* fmt, ...) {
    g_has_error = 1;

    if (fmt == NULL) {
        g_errbuf[0] = '\0';
        return;
    }

    va_list args;
    va_start(args, fmt);
    /*
     * vsnprintf always NUL-terminates and never overflows. If the message
     * is longer than 255 bytes, it is silently truncated. That's acceptable
     * on the error path; the code field still carries the semantic error.
     */
    vsnprintf(g_errbuf, sizeof(g_errbuf), fmt, args);
    va_end(args);

    /* Keep the code around in case we want to prefix the message later. */
    (void)code;
}

const char* _m_last_error(void) {
    return g_has_error ? g_errbuf : "";
}

void _m_clear_error(void) {
    g_has_error = 0;
    g_errbuf[0] = '\0';
}

#else  /* _M_ENABLE_ERROR_MESSAGES is off */

 /*
  * Messages are disabled. _m_set_error is still a real function so the
  * header's declaration is satisfied. It consumes its arguments at the
  * type level, which is why _M_RETURN_ERR's arguments don't produce
  * unused-variable warnings in callers.
  */
void _m_set_error(int32_t code, const char* fmt, ...) {
    (void)code;
    (void)fmt;
}

const char* _m_last_error(void) {
    return "";
}

void _m_clear_error(void) {
    /* nothing */
}

#endif

#if defined(_M_ENABLE_ERROR_LOGGING) && _M_ENABLE_ERROR_LOGGING
/*
 * Optional immediate logging. Enabled by a separate flag because a library
 * should not write to the caller's stderr without being asked. Useful for
 * a throwaway debug build where you want errors to be visible without
 * having to add logging code at every call site.
 */
#  include <stdio.h>
#  define _M_LOG_ERROR(msg) fprintf(stderr, "[_m] %s\n", (msg))
#else
#  define _M_LOG_ERROR(msg) ((void)0)
#endif