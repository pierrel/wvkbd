#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "glide.h"

static long long
nanoseconds(const struct timespec *start, const struct timespec *finish)
{
    return (long long)(finish->tv_sec - start->tv_sec) * 1000000000LL +
           finish->tv_nsec - start->tv_nsec;
}

int
main(int argc, char **argv)
{
    const char trace[] = "internationalizationinternationalizationinternationalizationin";
    long long maximum = 0;
    long long maximum_us = 50000;
    struct glide_match match;

    if (argc == 3 && strcmp(argv[1], "--max-us") == 0) {
        maximum_us = strtoll(argv[2], NULL, 10);
    } else if (argc != 1) {
        return 2;
    }
    for (int i = 0; i < 10; i++) {
        if (!glide_recognize(trace, sizeof(trace) - 1, &match)) return 1;
    }
    for (int i = 0; i < 1000; i++) {
        struct timespec start, finish;
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
        if (!glide_recognize(trace, sizeof(trace) - 1, &match)) return 1;
        clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
        long long elapsed = nanoseconds(&start, &finish);
        if (elapsed > maximum) maximum = elapsed;
    }
    printf("max-us=%lld\n", maximum / 1000);
    return maximum / 1000 <= maximum_us ? 0 : 1;
}
