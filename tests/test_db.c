#include "test_harness.h"

static void test_placeholder(void) {
    TEST_ASSERT(1 == 1);
}

int main(void) {
    TEST_RUN(test_placeholder);
    TEST_SUMMARY();
}