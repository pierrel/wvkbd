#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "glide.h"
#include "glide-words-en.h"

_Static_assert(sizeof(glide_word_bytes) <= 256 * 1024,
               "glide dictionary payload exceeds its bound");

static size_t
collapse(const char *input, size_t length, char output[GLIDE_MAX_WORD])
{
    size_t count = 0;

    for (size_t i = 0; i < length; i++) {
        if (input[i] < 'a' || input[i] > 'z') {
            return 0;
        }
        if (count == 0 || output[count - 1] != input[i]) {
            if (count == GLIDE_MAX_WORD) {
                return 0;
            }
            output[count++] = input[i];
        }
    }
    return count;
}

static unsigned int
distance(const char *left, size_t left_length, const char *right,
         size_t right_length)
{
    unsigned int previous[GLIDE_MAX_WORD + 1];
    unsigned int current[GLIDE_MAX_WORD + 1];

    for (size_t j = 0; j <= right_length; j++) {
        previous[j] = j;
    }
    for (size_t i = 1; i <= left_length; i++) {
        current[0] = i;
        for (size_t j = 1; j <= right_length; j++) {
            unsigned int substitution = previous[j - 1] +
                                        (left[i - 1] != right[j - 1]);
            unsigned int insertion = current[j - 1] + 1;
            unsigned int deletion = previous[j] + 1;
            unsigned int minimum = substitution < insertion ? substitution : insertion;
            current[j] = minimum < deletion ? minimum : deletion;
        }
        memcpy(previous, current, sizeof(previous));
    }
    return previous[right_length];
}

bool
glide_recognize(const char *trace, size_t length, struct glide_match *match)
{
    char collapsed_trace[GLIDE_MAX_WORD];
    size_t trace_length;
    const struct glide_bucket_range *bucket;
    unsigned int best_distance = UINT_MAX;
    size_t best_length_difference = SIZE_MAX;
    unsigned short best_rank = USHRT_MAX;
    const char *best_word = NULL;
    size_t best_length = 0;

    if (!match || !trace || length == 0 || length > GLIDE_MAX_TRACE) {
        return false;
    }
    trace_length = collapse(trace, length, collapsed_trace);
    if (trace_length == 0) {
        return false;
    }
    bucket = &glide_buckets[collapsed_trace[0] - 'a']
                            [collapsed_trace[trace_length - 1] - 'a'];
    for (unsigned int i = bucket->start; i < bucket->start + bucket->count; i++) {
        const char *word = (const char *)&glide_word_bytes[glide_word_offsets[i]];
        char signature[GLIDE_MAX_WORD];
        size_t word_length = strlen(word);
        size_t signature_length = collapse(word, word_length, signature);
        unsigned int word_distance = distance(collapsed_trace, trace_length,
                                              signature, signature_length);
        size_t length_difference = trace_length > signature_length
                                       ? trace_length - signature_length
                                       : signature_length - trace_length;
        size_t maximum = trace_length > signature_length ? trace_length : signature_length;

        if (3 * word_distance > maximum) {
            continue;
        }
        if (!best_word || word_distance < best_distance ||
            (word_distance == best_distance &&
             (length_difference < best_length_difference ||
              (length_difference == best_length_difference &&
               (glide_word_ranks[i] < best_rank ||
                (glide_word_ranks[i] == best_rank && strcmp(word, best_word) < 0)))))) {
            best_distance = word_distance;
            best_length_difference = length_difference;
            best_rank = glide_word_ranks[i];
            best_word = word;
            best_length = word_length;
        }
    }
    if (!best_word) {
        return false;
    }
    match->word = best_word;
    match->length = best_length;
    return true;
}
