#ifndef GLIDE_H
#define GLIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLIDE_MAX_TRACE 64
#define GLIDE_MAX_WORD 24
#define GLIDE_PATH_SAMPLES 32
#define GLIDE_MAX_MATCHES 3
#define GLIDE_FEEDBACK_MAX 32
#define GLIDE_FEEDBACK_SNAPSHOT_MAX 5964
#define GLIDE_ALGORITHM "geometry-feedback-v1"

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

struct glide_feedback_entry {
    char trace[GLIDE_MAX_TRACE + 1];
    char word[GLIDE_MAX_WORD + 1];
    uint16_t corrections;
};

struct glide_feedback {
    size_t count;
    struct glide_feedback_entry entries[GLIDE_FEEDBACK_MAX];
};

extern const char glide_algorithm[];
extern const char glide_dictionary_sha256[];

void glide_recognize(const char *trace, const struct glide_point *points,
                     size_t length, const struct glide_geometry *geometry,
                     struct glide_result *result);
void glide_recognize_with_feedback(
    const char *trace, const struct glide_point *points, size_t length,
    const struct glide_geometry *geometry, const struct glide_feedback *feedback,
    struct glide_result *result);
bool glide_feedback_parse(struct glide_feedback *feedback, char *snapshot,
                          size_t length);
bool glide_feedback_reject(struct glide_feedback *feedback, const char *trace,
                           size_t trace_length, const char *word,
                           size_t word_length);

#endif
