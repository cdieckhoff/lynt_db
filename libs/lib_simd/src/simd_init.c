/**
 * @file simd_init.c
 * @brief Runtime ISA detection and dispatch table initialization.
 *
 * Runs once, on first use of any simd_* function. After that, the
 * dispatch table is read-only and every public call is a single
 * function-pointer indirection.
 *
 * COMPILER FLAGS
 * --------------
 * This file MUST be compiled with a target architecture flag that
 * enables AVX. On MSVC that's /arch:AVX; on GCC/Clang it's -mavx.
 * The reason is _xgetbv: MSVC only declares it when the compiler is
 * targeting an architecture that supports the XGETBV instruction.
 * Without the flag, MSVC sees the call site, assumes _xgetbv returns
 * int, and emits garbage. The detection then fails silently and the
 * library falls back to scalar.
 *
 * /arch:AVX (not /arch:AVX2) is the right level: any CPU with AVX2
 * has AVX, so the detection file can run on every CPU the library
 * cares about. Using /arch:AVX2 would make the detection code itself
 * require AVX2, which is the thing it's trying to check for.
 *
 * PORTABLE ONE-TIME INITIALIZATION
 * --------------------------------
 * MSVC's <stdatomic.h> requires /experimental:c11atomics, which is
 * documented as experimental. Rather than depend on it, we use the
 * native one-time-init primitive each platform provides:
 *
 *   Windows: InitOnceExecuteOnce (Vista and later).
 *   POSIX:   pthread_once.
 *
 * DETECTION ORDER (x86-64)
 * ------------------------
 *   AVX2 + FMA  → best throughput, ~2013+ CPUs
 *   SSE4.2      → fallback, ~2008+ CPUs
 *   scalar      → last resort
 *
 * DETECTION ORDER (ARM64)
 * -----------------------
 *   NEON        → guaranteed on ARMv8-A
 */

#include "simd/simd_internal.h"

#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__x86_64__) || defined(__aarch64__)
#  include <pthread.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#  if defined(_MSC_VER)
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#endif

 /* The one and only dispatch table. Filled exactly once. */
struct simd_backend g_simd;

/* ====================================================================
 * x86-64 feature detection
 * ==================================================================== */

#if defined(__x86_64__) || defined(_M_X64)

 /**
  * @brief Check whether the CPU supports AVX2 + FMA.
  *
  * We require both. Every AVX2-era CPU has FMA, but the check is
  * cheap and defends against hypothetical future CPUs that ship AVX2
  * without FMA.
  *
  * The MSVC path uses __cpuid, __cpuidex, and _xgetbv directly. The
  * GCC/Clang path uses the __builtin_cpu_supports helper, which caches
  * the CPUID result after the first call.
  */
static bool cpu_has_avx2_fma(void) {
#if defined(_MSC_VER)
    int info[4] = { 0 };

    __cpuid(info, 0);
    if (info[0] < 7) return false;

    __cpuidex(info, 1, 0);
    if ((info[2] & (1 << 27)) == 0) return false;   /* OSXSAVE */
    if ((info[2] & (1 << 28)) == 0) return false;   /* AVX */

    unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) return false;          /* XMM + YMM state */

    __cpuidex(info, 7, 0);
    if ((info[1] & (1 << 5)) == 0) return false;    /* AVX2 (leaf 7 EBX) */

    __cpuidex(info, 1, 0);
    if ((info[2] & (1 << 12)) == 0) return false;   /* FMA  (leaf 1 ECX) */

    return true;
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
}

/**
 * @brief Check whether the CPU supports SSE4.2.
 *
 * Present on every x86-64 CPU made since roughly 2008.
 */
static bool cpu_has_sse42(void) {
#if defined(_MSC_VER)
    int info[4] = { 0 };
    __cpuid(info, 1);
    return (info[2] & (1 << 20)) != 0;   /* SSE4.2 (leaf 1 ECX) */
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.2");
#endif
}

#endif  /* x86-64 */

/* ====================================================================
 * The initialization function
 * ==================================================================== */

static void simd_do_init(void) {
#if defined(__x86_64__) || defined(_M_X64)
    if (cpu_has_avx2_fma()) {
        memcpy(&g_simd, simd_backend_avx2(), sizeof(g_simd));
    }
    else if (cpu_has_sse42()) {
        memcpy(&g_simd, simd_backend_sse42(), sizeof(g_simd));
    }
    else {
        memcpy(&g_simd, simd_backend_scalar(), sizeof(g_simd));
    }

#elif defined(__aarch64__) || defined(_M_ARM64)
    memcpy(&g_simd, simd_backend_neon(), sizeof(g_simd));

#else
    memcpy(&g_simd, simd_backend_scalar(), sizeof(g_simd));
#endif
}

/* ====================================================================
 * Platform-specific one-time init wrapper
 * ==================================================================== */

#if defined(_WIN32)

static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK simd_init_once_callback(PINIT_ONCE once, PVOID param,
    PVOID* ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    simd_do_init();
    return TRUE;
}

simd_isa_t simd_init(void) {
    InitOnceExecuteOnce(&g_init_once, simd_init_once_callback, NULL, NULL);
    return g_simd.isa;
}

#else

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

static void simd_init_once_callback(void) {
    simd_do_init();
}

simd_isa_t simd_init(void) {
    pthread_once(&g_init_once, simd_init_once_callback);
    return g_simd.isa;
}

#endif

simd_isa_t simd_active_isa(void) {
    simd_init();
    return g_simd.isa;
}

const char* simd_isa_name(simd_isa_t isa) {
    switch (isa) {
    case SIMD_ISA_SCALAR: return "scalar";
    case SIMD_ISA_SSE42:  return "sse4.2";
    case SIMD_ISA_AVX2:   return "avx2";
    case SIMD_ISA_NEON:   return "neon";
    default:              return "unknown";
    }
}