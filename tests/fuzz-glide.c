#include <stddef.h>
#include <stdint.h>

#include "glide.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char valid_trace[] = "helo";
    struct glide_geometry geometry = {0};
    struct glide_point points[GLIDE_MAX_TRACE];
    struct glide_match match;
    const char *trace = (const char *)data;
    size_t trace_length = size;

    geometry.complete = size == 0 || (data[0] & 1) != 0;
    geometry.key_height = size == 0 || (data[0] & 2) == 0 ? 1 : 0;
    for (size_t i = 0; i < 26; i++) {
        uint32_t value = (uint32_t)(i * 7919);

        for (size_t j = 0; size && j < 4; j++) {
            value = (value << 8) | data[(i + j) % size];
        }
        geometry.letters[i] = (struct glide_point){.x = (int32_t)value,
                                                    .y = (int32_t)~value};
    }
    if (size && (data[0] & 4)) {
        trace = valid_trace;
        trace_length = sizeof(valid_trace) - 1;
    }
    if (trace_length > GLIDE_MAX_TRACE) {
        glide_recognize(trace, NULL, trace_length, &geometry, &match);
        return 0;
    }
    for (size_t i = 0; i < trace_length; i++) {
        uint32_t value = size ? data[i % size] : 0;

        for (size_t j = 1; size && j < 4; j++) {
            value |= (uint32_t)data[(i + j) % size] << (8 * j);
        }
        points[i] = (struct glide_point){.x = (int32_t)value,
                                         .y = (int32_t)~value};
    }
    glide_recognize(trace, points, trace_length, &geometry, &match);
    return 0;
}
