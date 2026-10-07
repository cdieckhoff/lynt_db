#ifndef LYNT_TEST_HARNESS_H
#define LYNT_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond)                                                  \
    do {                                                                   \
        g_tests_run++;                                                     \
        if (!(cond)) {                                                     \
            g_tests_failed++;                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n",                            \
                    __FILE__, __LINE__, #cond);                            \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_EQ_INT(a, b)                                           \
    do {                                                                   \
        g_tests_run++;                                                     \
        long long _a = (long long)(a);                                     \
        long long _b = (long long)(b);                                     \
        if (_a != _b) {                                                    \
            g_tests_failed++;                                              \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%lld != %lld)\n",       \
                    __FILE__, __LINE__, #a, #b, _a, _b);                   \
        }                                                                  \
    } while (0)

#define TEST_RUN(fn)                                                       \
    do {                                                                   \
        fprintf(stdout, "  running %s\n", #fn);                            \
        fflush(stdout);                                                    \
        fn();                                                              \
        fflush(stdout);                                                    \
    } while (0)

#define TEST_SUMMARY()                                                     \
    do {                                                                   \
        fprintf(stdout, "%d run, %d failed\n",                             \
                g_tests_run, g_tests_failed);                              \
        return g_tests_failed == 0 ? 0 : 1;                                \
    } while (0)

#endif