/**
 * @file _m_error.h
 * @brief Error message capture and retrieval.
 *
 * THE MODEL
 * ---------
 * The _m layer uses a two-part error model:
 *
 *   1. Every function returns a result struct whose `code` field carries a
 *      compact error enumeration (_m_err_t). This is what callers branch on.
 *
 *   2. If the build flag _M_ENABLE_ERROR_MESSAGES is set, a human-readable
 *      message is stored in a thread-local buffer accessible via
 *      _m_last_error(). This is what callers log or display.
 *
 * WHY TWO PARTS
 * -------------
 * The code is cheap (an int). The message is expensive (a 256-byte thread
 * local + a vsnprintf call). In a hot loop that only branches on success,
 * you want the code and NOT the message. In a top-level handler that's about
 * to log a failure, you want the message.
 *
 * THREAD SAFETY
 * -------------
 * The message buffer is _Thread_local (C11). Each thread has its own. You
 * can call _m_last_error() concurrently from many threads with no locking.
 * However, the message reflects the LAST error on THAT thread, so if you
 * call multiple _m_* functions in sequence, only the most recent failure's
 * message survives. Capture it immediately.
 *
 * BUILD FLAGS
 * -----------
 *   _M_ENABLE_ERROR_MESSAGES
 *       When set, _m_set_error() stores a formatted message and
 *       _m_last_error() returns it. When unset, both are no-ops and
 *       _m_last_error() returns "".
 *       Default: ON in Debug builds, OFF in Release (set by CMake).
 *
 *   _M_ENABLE_ERROR_LOGGING
 *       When set, _m_set_error() ALSO writes the message to stderr
 *       immediately. Useful for a throwaway debug build; not appropriate
 *       for a library, since it writes to the caller's stderr.
 *       Default: OFF.
 *
 * USAGE
 * -----
 *     _m_result_t r = _m_open(path, _M_O_RDWR, 0, false);
 *     if (r.code != _M_OK) {
 *         fprintf(stderr, "open failed: %s\n", _m_last_error());
 *     }
 *
 * UNUSED-VARIABLE WARNINGS
 * ------------------------
 * A common call-site pattern is:
 *
 *     int err = errno;
 *     _M_RETURN_ERR(_M_ERR_IO, "... (errno=%d)", err);
 *
 * When _M_ENABLE_ERROR_MESSAGES is off, _M_RETURN_ERR must not use its
 * arguments. A naive macro that discards the arguments via ((void)0)
 * makes `err` unused, which Clang warns about (-Wunused-variable).
 * GCC and MSVC are more permissive and don't warn, which is why this
 * only surfaces on macOS. To avoid the warning on every compiler, we
 * route the arguments through _m_consume(), a static inline that always
 * takes them. The call has zero cost after inlining.
 */

#ifndef M__M_ERROR_H
#define M__M_ERROR_H

#include "m/_m_types.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Retrieve the last error message set on the current thread.
     *
     * @return A NUL-terminated string. Never NULL. Returns "" if no error has
     *         been recorded on this thread, or if _M_ENABLE_ERROR_MESSAGES
     *         is not defined at build time.
     *
     * @note The returned pointer is valid until the next _m_set_error() call
     *       on the same thread. Do not free it. Do not retain it across calls.
     */
    const char* _m_last_error(void);

    /**
     * @brief Clear the per-thread error message.
     *
     * Called at the top of each public _m_* function so that a stale message
     * from a previous failure doesn't survive into a later success.
     */
    void _m_clear_error(void);

    /**
     * @brief Record an error code and message on the current thread.
     *
     * This is an implementation detail of the _m layer. Callers normally use
     * the _M_RETURN_ERR macro, which compiles away the message capture
     * entirely when messages are disabled.
     *
     * @param code  The _m_err_t value.
     * @param fmt   printf-style format string, or NULL for no message.
     * @param ...   Format arguments.
     *
     * @note The stored message is truncated to 255 bytes plus NUL. Long paths
     *       or error strings will be cut off; that's deliberate, since the
     *       buffer is fixed-size to avoid allocation on the error path.
     *
     * @note The variadic declaration is unconditional so that _m_set_error
     *       always has a real signature. When messages are disabled, the
     *       definition in _m_error.c is an empty function; the arguments
     *       are still consumed at the type level, which keeps callers happy.
     */
    void _m_set_error(int32_t code, const char* fmt, ...);

    /**
     * @brief Consume a variadic argument list and discard it.
     *
     * Used to keep the compiler from warning about variables that are only
     * referenced in arguments to _M_RETURN_ERR when error messages are
     * disabled. The call is optimized away entirely by every compiler we
     * care about, so this has no runtime cost.
     *
     * This exists because Clang is strict about "declared but not used"
     * variables even when the code that references them is inside a macro
     * that expands to a no-op. GCC and MSVC are permissive; Clang is not.
     * Running CI on macOS is the only way to catch this class of issue
     * before users do.
     *
     * The `__attribute__((unused))` on the parameter tells the compiler
     * that we know we're not using it, so -Wunused-parameter (which is
     * part of -Wextra) doesn't fire on this function itself.
     */
    static inline void _m_consume(int32_t code, ...) {
        (void)code;
    }

    /* ====================================================================
     * Internal macros used by _m_* implementations
     * ==================================================================== */

#if defined(_M_ENABLE_ERROR_MESSAGES) && _M_ENABLE_ERROR_MESSAGES

     /**
      * Record an error with a formatted message. Only the message is
      * captured; the caller constructs the _m_result_t with the same
      * code via _m_err(code).
      */
#define _M_SET_ERROR(code, ...) _m_set_error((code), __VA_ARGS__)

      /**
       * Record an error and return it in one expression. Use at the tail
       * of a failing branch:
       *
       *     if (h == INVALID_HANDLE_VALUE) {
       *         _M_RETURN_ERR(_M_ERR_IO, "_m_open: CreateFileW failed (%lu)",
       *                       GetLastError());
       *     }
       *
       * Variadic arguments are forwarded to _m_set_error, which formats
       * them into the thread-local buffer.
       */
#define _M_RETURN_ERR(code, ...)                                          \
        do {                                                                  \
            _m_set_error((code), __VA_ARGS__);                                \
            return _m_err((code));                                            \
        } while (0)

#define _M_RETURN_ERR_PTR(code, ...)                                      \
        do {                                                                  \
            _m_set_error((code), __VA_ARGS__);                                \
            return _m_err_ptr((code));                                        \
        } while (0)

#else  /* _M_ENABLE_ERROR_MESSAGES is off */

     /*
      * Messages are disabled. We still call _m_consume() with the
      * variadic arguments so the compiler sees every argument as used.
      * Otherwise Clang warns about variables that are only referenced
      * in these macros.
      */
#define _M_SET_ERROR(code, ...)                                           \
        do { (void)(code); } while (0)

#define _M_RETURN_ERR(code, ...)                                          \
        do {                                                                  \
            _m_consume((code), __VA_ARGS__);                                  \
            return _m_err((code));                                            \
        } while (0)

#define _M_RETURN_ERR_PTR(code, ...)                                      \
        do {                                                                  \
            _m_consume((code), __VA_ARGS__);                                  \
            return _m_err_ptr((code));                                        \
        } while (0)

#endif

#ifdef __cplusplus
}
#endif

#endif /* M__M_ERROR_H */