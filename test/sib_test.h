/* Minimal check harness shared by the host tests. Counts, reports the first
 * line that failed, and makes the exit code mean something to CI. */
#pragma once

#include <stdio.h>
#include <stdlib.h>

static int sib_checks = 0;
static int sib_fails = 0;

#define CHECK(cond, ...)                              \
    do {                                              \
        sib_checks++;                                 \
        if(!(cond)) {                                 \
            sib_fails++;                              \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                      \
            printf("\n");                             \
        }                                             \
    } while(0)

#define CHECK_RANGE(v, lo, hi, name)                                       \
    do {                                                                   \
        long _v = (long)(v);                                               \
        CHECK(_v >= (long)(lo) && _v <= (long)(hi), "%s = %ld, want %ld..%ld", \
              name, _v, (long)(lo), (long)(hi));                           \
    } while(0)

static int sib_report(const char* suite) {
    printf("%s: %d checks, %d failed\n", suite, sib_checks, sib_fails);
    return sib_fails ? 1 : 0;
}
