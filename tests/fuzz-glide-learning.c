#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "glide-learning.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char session[] = "0123456789abcdef0123456789abcdef";
    struct glide_point points[GLIDE_MAX_TRACE];
    struct glide_geometry geometry = {.complete = true, .key_height = 1};
    struct glide_match matches[GLIDE_MAX_MATCHES];
    struct glide_result result = {.matches = {{0}}};
    char trace[GLIDE_MAX_TRACE];
    char words[GLIDE_MAX_MATCHES][GLIDE_MAX_WORD];
    char output[GLIDE_LEARNING_JSON_MAX];
    char snapshot[GLIDE_FEEDBACK_SNAPSHOT_MAX + 1];
    struct glide_feedback feedback;
    size_t trace_length;

    if (!size)
        return 0;
    trace_length = 2 + data[0] % (GLIDE_MAX_TRACE - 1);
    result.count = size > 1 ? data[1] % (GLIDE_MAX_MATCHES + 1) : 0;
    for (size_t i = 0; i < trace_length; i++) {
        trace[i] = (char)('a' + data[i % size] % 26);
        points[i] = (struct glide_point){.x = data[i % size],
                                         .y = -(int32_t)data[i % size]};
    }
    for (size_t i = 0; i < 26; i++)
        geometry.letters[i] = (struct glide_point){.x = (int32_t)i,
                                                    .y = -(int32_t)i};
    for (size_t i = 0; i < result.count; i++) {
        size_t length = 2 + data[(i + 2) % size] % (GLIDE_MAX_WORD - 1);
        for (size_t j = 0; j < length; j++)
            words[i][j] = (char)('a' + data[(i + j) % size] % 26);
        matches[i] = (struct glide_match){.word = words[i],
                                          .length = length,
                                          .score = data[i % size]};
        result.matches[i] = matches[i];
    }
    glide_learning_encode_gesture(session, 1, trace, points, trace_length,
                                  &geometry, &result, output);
    glide_learning_encode_resolution(
        session, 1, GLIDE_LEARNING_ALTERNATE_SELECTED,
        result.count > 1 ? 2 : 0, output);
    if (size <= GLIDE_FEEDBACK_SNAPSHOT_MAX) {
        memcpy(snapshot, data, size);
        snapshot[size] = '\0';
        glide_feedback_parse(&feedback, snapshot, size);
    }
    return 0;
}
