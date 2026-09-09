#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "glide.h"
#include "glide-words-en.h"

_Static_assert(GLIDE_LARGEST_BUCKET_COUNT <= 512,
               "benchmark bucket exceeds the candidate bound");
_Static_assert(GLIDE_LARGEST_BUCKET_COUNT * GLIDE_MAX_TRACE * GLIDE_MAX_WORD <=
                   512 * 64 * 24,
               "benchmark exceeds the cell bound");

static long long
nanoseconds(const struct timespec *start, const struct timespec *finish)
{
    return (long long)(finish->tv_sec - start->tv_sec) * 1000000000LL +
           finish->tv_nsec - start->tv_nsec;
}

int
main(int argc, char **argv)
{
    static const char trace[] = "cccccccc"
                                "ooooooo"
                                "uuuuuuu"
                                "nnnnnnn"
                                "ttttttt"
                                "rrrrrrr"
                                "iiiiiii"
                                "eeeeeee"
                                "sssssss";
    static const char expected[] = "countries";
    long long maximum = 0;
    long long maximum_ns = 0;
    struct glide_match match;

    if (sizeof(trace) - 1 != GLIDE_MAX_TRACE ||
        trace[0] != GLIDE_LARGEST_BUCKET_FIRST ||
        trace[sizeof(trace) - 2] != GLIDE_LARGEST_BUCKET_LAST ||
        glide_buckets[trace[0] - 'a'][trace[sizeof(trace) - 2] - 'a'].count !=
            GLIDE_LARGEST_BUCKET_COUNT) {
        return 1;
    }

    if (argc == 1) {
        return glide_recognize(trace, sizeof(trace) - 1, &match) &&
                       match.length == sizeof(expected) - 1 &&
                       memcmp(match.word, expected, sizeof(expected) - 1) == 0
                   ? 0
                   : 1;
    }
    if (argc == 3 && strcmp(argv[1], "--max-us") == 0) {
        char *end;
        long long maximum_us;

        errno = 0;
        maximum_us = strtoll(argv[2], &end, 10);
        if (errno || *argv[2] == '\0' || *end != '\0' || maximum_us < 0 ||
            maximum_us > LLONG_MAX / 1000) {
            return 2;
        }
        maximum_ns = maximum_us * 1000;
    } else {
        return 2;
    }
    for (int i = 0; i < 10; i++) {
        if (!glide_recognize(trace, sizeof(trace) - 1, &match) ||
            match.length != sizeof(expected) - 1 ||
            memcmp(match.word, expected, sizeof(expected) - 1) != 0)
            return 1;
    }
    for (int i = 0; i < 1000; i++) {
        struct timespec start, finish;
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
        if (!glide_recognize(trace, sizeof(trace) - 1, &match) ||
            match.length != sizeof(expected) - 1 ||
            memcmp(match.word, expected, sizeof(expected) - 1) != 0)
            return 1;
        clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
        long long elapsed = nanoseconds(&start, &finish);
        if (elapsed > maximum)
            maximum = elapsed;
    }
    printf("max-us=%lld\n", maximum / 1000);
    return maximum > maximum_ns ? 1 : 0;
}
