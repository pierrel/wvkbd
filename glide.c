#include <stdint.h>
#include <string.h>

#include "glide.h"
#include "glide-words-en.h"

const char glide_dictionary_sha256[] = GLIDE_DICTIONARY_SHA256;

_Static_assert(sizeof(glide_word_bytes) + sizeof(glide_buckets) <= 256 * 1024,
               "glide dictionary payload exceeds its bound");

static size_t
collapse(const char *input, size_t length, char *output, size_t capacity)
{
    size_t count = 0;

    for (size_t i = 0; i < length; i++) {
        if (input[i] < 'a' || input[i] > 'z') {
            return 0;
        }
        if (count == 0 || output[count - 1] != input[i]) {
            if (count == capacity) {
                return 0;
            }
            output[count++] = input[i];
        }
    }
    return count;
}

static uint64_t
magnitude(int32_t left, int32_t right)
{
    int64_t difference = (int64_t)left - right;

    return difference < 0 ? (uint64_t)-difference : (uint64_t)difference;
}

static uint64_t
segment_length(const struct glide_point *left, const struct glide_point *right)
{
    return magnitude(left->x, right->x) + magnitude(left->y, right->y);
}

static bool
resample(const struct glide_point *points, size_t length,
         struct glide_point samples[GLIDE_PATH_SAMPLES])
{
    uint64_t total = 0;
    size_t segment = 0;
    uint64_t consumed = 0;

    if (!points || length < 2 || length > GLIDE_MAX_TRACE) {
        return false;
    }
    for (size_t i = 0; i + 1 < length; i++) {
        total += segment_length(&points[i], &points[i + 1]);
    }
    if (total == 0) {
        return false;
    }
    for (size_t i = 0; i < GLIDE_PATH_SAMPLES; i++) {
        uint64_t target;
        uint64_t current;
        uint64_t end;
        uint64_t remainder;
        const struct glide_point *left;
        const struct glide_point *right;
        __int128 x;
        __int128 y;

        if (i + 1 == GLIDE_PATH_SAMPLES) {
            samples[i] = points[length - 1];
            continue;
        }
        target = (uint64_t)(((unsigned __int128)total * i) /
                            (GLIDE_PATH_SAMPLES - 1));
        for (;;) {
            if (segment + 1 >= length) {
                return false;
            }
            current = segment_length(&points[segment], &points[segment + 1]);
            if (current == 0) {
                segment++;
                continue;
            }
            end = consumed + current;
            if (target < end) {
                break;
            }
            consumed = end;
            segment++;
        }
        left = &points[segment];
        right = &points[segment + 1];
        remainder = target - consumed;
        x = (__int128)left->x +
            ((__int128)((int64_t)right->x - left->x) * remainder) / current;
        y = (__int128)left->y +
            ((__int128)((int64_t)right->y - left->y) * remainder) / current;
        if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
            return false;
        }
        samples[i] = (struct glide_point){.x = (int32_t)x, .y = (int32_t)y};
    }
    return true;
}

static uint64_t
score(const struct glide_point gesture[GLIDE_PATH_SAMPLES],
      const struct glide_point candidate[GLIDE_PATH_SAMPLES])
{
    uint64_t total = 0;

    for (size_t i = 0; i < GLIDE_PATH_SAMPLES; i++) {
        total += magnitude(gesture[i].x, candidate[i].x) +
                 magnitude(gesture[i].y, candidate[i].y);
    }
    return total / GLIDE_PATH_SAMPLES + (total % GLIDE_PATH_SAMPLES != 0);
}

struct ranked_match {
    struct glide_match match;
    uint64_t error;
};

static void
keep_match(struct ranked_match matches[GLIDE_MAX_MATCHES], size_t *count,
           const char *word, size_t length, uint64_t error)
{
    size_t position = 0;

    while (position < *count && matches[position].error <= error) {
        position++;
    }
    if (position == GLIDE_MAX_MATCHES) {
        return;
    }
    if (*count < GLIDE_MAX_MATCHES) {
        (*count)++;
    }
    for (size_t i = *count - 1; i > position; i--) {
        matches[i] = matches[i - 1];
    }
    matches[position] = (struct ranked_match){
        .match = {.word = word, .length = length, .score = error},
        .error = error,
    };
}

void
glide_recognize(const char *trace, const struct glide_point *points,
                size_t length, const struct glide_geometry *geometry,
                struct glide_result *result)
{
    struct glide_point gesture[GLIDE_PATH_SAMPLES];
    const struct glide_bucket_range *bucket;
    struct ranked_match matches[GLIDE_MAX_MATCHES] = {0};
    size_t match_count = 0;
    uint64_t threshold;

    if (!result) {
        return;
    }
    *result = (struct glide_result){0};
    if (!trace || !points || !geometry || !geometry->complete ||
        geometry->key_height == 0 || length < 2 || length > GLIDE_MAX_TRACE ||
        trace[0] < 'a' || trace[0] > 'z' || trace[length - 1] < 'a' ||
        trace[length - 1] > 'z') {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        if (trace[i] < 'a' || trace[i] > 'z') {
            return;
        }
    }
    if (!resample(points, length, gesture)) {
        return;
    }
    threshold = ((uint64_t)geometry->key_height * 9) / 10;
    if (threshold < 12) {
        threshold = 12;
    }
    bucket = &glide_buckets[trace[0] - 'a'][trace[length - 1] - 'a'];
    if (bucket->count > 512 || bucket->byte_start > bucket->byte_end ||
        bucket->byte_end > sizeof(glide_word_bytes) - 1) {
        return;
    }
    const unsigned char *word = &glide_word_bytes[bucket->byte_start];
    const unsigned char *end = &glide_word_bytes[bucket->byte_end];
    for (unsigned int offset = 0; offset < bucket->count; offset++) {
        struct glide_point vertices[GLIDE_MAX_WORD];
        struct glide_point candidate[GLIDE_PATH_SAMPLES];
        char signature[GLIDE_MAX_WORD];
        const unsigned char *terminator =
            memchr(word, '\0', (size_t)(end - word));
        size_t word_size;
        size_t signature_length;
        uint64_t error;

        if (!terminator) {
            return;
        }
        word_size = (size_t)(terminator - word);
        if (word_size < 2 || word_size > GLIDE_MAX_WORD) {
            return;
        }
        for (size_t i = 0; i < word_size; i++) {
            if (word[i] < 'a' || word[i] > 'z') {
                return;
            }
        }
        signature_length = collapse((const char *)word, word_size, signature,
                                    sizeof(signature));
        if (signature_length >= 2) {
            for (size_t i = 0; i < signature_length; i++) {
                vertices[i] = geometry->letters[signature[i] - 'a'];
            }
            if (!resample(vertices, signature_length, candidate)) {
                return;
            }
            error = score(gesture, candidate);
            if (error <= threshold) {
                keep_match(matches, &match_count, (const char *)word, word_size,
                           error);
            }
        }
        word = terminator + 1;
    }
    if (word != end) {
        return;
    }
    result->count = match_count;
    for (size_t i = 0; i < match_count; i++) {
        result->matches[i] = matches[i].match;
    }
}
