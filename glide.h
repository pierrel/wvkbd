#ifndef GLIDE_H
#define GLIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLIDE_MAX_TRACE 64
#define GLIDE_MAX_WORD 24
#define GLIDE_PATH_SAMPLES 32
#define GLIDE_MAX_MATCHES 3

struct glide_point {
    int32_t x;
    int32_t y;
};

struct glide_geometry {
    struct glide_point letters[26];
    uint32_t key_height;
    bool complete;
};

struct glide_match {
    const char *word;
    size_t length;
    uint64_t score;
};

struct glide_result {
    size_t count;
    struct glide_match matches[GLIDE_MAX_MATCHES];
};

extern const char glide_dictionary_sha256[];

void glide_recognize(const char *trace, const struct glide_point *points,
                     size_t length, const struct glide_geometry *geometry,
                     struct glide_result *result);

#endif
