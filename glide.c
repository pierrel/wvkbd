#include <stdint.h>

#include "glide.h"
#include "glide-words-en.h"

_Static_assert(sizeof(glide_word_bytes) + sizeof(glide_buckets) <=
                   256 * 1024,
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

static size_t
word_length(const char *word)
{
    size_t length = 0;

    while (length <= GLIDE_MAX_WORD && word[length]) {
        length++;
    }
    return length;
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

bool
glide_recognize(const char *trace, const struct glide_point *points,
                size_t length, const struct glide_geometry *geometry,
                struct glide_match *match)
{
    struct glide_point gesture[GLIDE_PATH_SAMPLES];
    const struct glide_bucket_range *bucket;
    const char *best_word = NULL;
    size_t best_length = 0;
    uint64_t best_error = UINT64_MAX;
    uint64_t runner_error = UINT64_MAX;
    uint64_t threshold;

    if (!match || !trace || !points || !geometry || !geometry->complete ||
        geometry->key_height == 0 || length < 2 || length > GLIDE_MAX_TRACE ||
        trace[0] < 'a' || trace[0] > 'z' || trace[length - 1] < 'a' ||
        trace[length - 1] > 'z') {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (trace[i] < 'a' || trace[i] > 'z') {
            return false;
        }
    }
    if (!resample(points, length, gesture)) {
        return false;
    }
    bucket = &glide_buckets[trace[0] - 'a'][trace[length - 1] - 'a'];
    const char *word = (const char *)&glide_word_bytes[bucket->byte_start];
    for (unsigned int offset = 0; offset < bucket->count; offset++) {
        struct glide_point vertices[GLIDE_MAX_WORD];
        struct glide_point candidate[GLIDE_PATH_SAMPLES];
        char signature[GLIDE_MAX_WORD];
        size_t word_size = word_length(word);
        size_t signature_length;
        uint64_t error;
        if (word_size == 0 || word_size > GLIDE_MAX_WORD) {
            return false;
        }
        signature_length =
            collapse(word, word_size, signature, sizeof(signature));
        if (signature_length >= 2) {
            for (size_t i = 0; i < signature_length; i++) {
                vertices[i] = geometry->letters[signature[i] - 'a'];
            }
            if (!resample(vertices, signature_length, candidate)) {
                return false;
            }
            error = score(gesture, candidate);
            if (!best_word || error < best_error) {
                if (best_word && best_error < runner_error) {
                    runner_error = best_error;
                }
                best_word = word;
                best_length = word_size;
                best_error = error;
            } else if (error < runner_error) {
                runner_error = error;
            }
        }
        word += word_size + 1;
    }
    threshold = ((uint64_t)geometry->key_height * 9) / 10;
    if (threshold < 12) {
        threshold = 12;
    }
    if (!best_word || best_error > threshold ||
        (runner_error != UINT64_MAX && runner_error <= best_error)) {
        return false;
    }
    match->word = best_word;
    match->length = best_length;
    return true;
}
