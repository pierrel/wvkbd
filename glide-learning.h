#ifndef GLIDE_LEARNING_H
#define GLIDE_LEARNING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "glide.h"

#define GLIDE_LEARNING_JSON_MAX 4096

enum glide_learning_outcome {
    GLIDE_LEARNING_TOP_COMMITTED,
    GLIDE_LEARNING_ALTERNATE_SELECTED,
    GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE,
    GLIDE_LEARNING_EXPLICIT_USER_MISSWIPE,
    GLIDE_LEARNING_RETRACTED_THEN_RESWIPED,
    GLIDE_LEARNING_MANUAL_CORRECTION_AMBIGUOUS,
};

struct glide_learning_sink {
    int fd;
    char session[33];
    uint64_t next_gesture;
    uint64_t pending_gesture;
    bool pending_has_candidates;
    bool correction_pending;
    struct glide_feedback feedback;
};

size_t glide_learning_encode_gesture(const char *session, uint64_t gesture,
                                     const char *trace,
                                     const struct glide_point *points,
                                     size_t trace_length,
                                     const struct glide_geometry *geometry,
                                     const struct glide_result *result,
                                     char output[GLIDE_LEARNING_JSON_MAX]);
size_t glide_learning_encode_resolution(
    const char *session, uint64_t gesture, enum glide_learning_outcome outcome,
    size_t selected_rank, char output[GLIDE_LEARNING_JSON_MAX]);
void glide_learning_sink_init(struct glide_learning_sink *sink, int fd);
bool glide_learning_observe(struct glide_learning_sink *sink, const char *trace,
                            const struct glide_point *points,
                            size_t trace_length,
                            const struct glide_geometry *geometry,
                            const struct glide_result *result);
void glide_learning_resolve(struct glide_learning_sink *sink,
                            enum glide_learning_outcome outcome,
                            size_t selected_rank);
void glide_learning_mark_retracted(struct glide_learning_sink *sink);
void glide_learning_resolve_correction(struct glide_learning_sink *sink,
                                       bool reswiped);
void glide_learning_reject(struct glide_learning_sink *sink, const char *trace,
                           size_t trace_length, const char *word,
                           size_t word_length);
void glide_learning_clear(struct glide_learning_sink *sink);

#endif
