
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glide.h"
#include "glide-words-en.h"

const char glide_algorithm[] = GLIDE_ALGORITHM;
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

static bool
valid_word(const char *word, size_t length)
{
    if (!word || length < 2 || length > GLIDE_MAX_WORD)
        return false;
    for (size_t i = 0; i < length; i++)
        if (word[i] < 'a' || word[i] > 'z')
            return false;
    return true;
}

static bool
valid_trace(const char *trace, size_t length)
{
    if (!trace || length < 2 || length > GLIDE_MAX_TRACE)
        return false;
    for (size_t i = 0; i < length; i++)
        if (trace[i] < 'a' || trace[i] > 'z')
            return false;
    return true;
}

static bool
valid_hex(const char *value, size_t length)
{
    if (!value || strlen(value) != length)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    return true;
}

static bool
feedback_add(struct glide_feedback *feedback, const char *trace,
             size_t trace_length, const char *word, size_t word_length,
             uint16_t corrections)
{
    struct glide_feedback_entry entry = {.corrections = corrections};
    size_t position;

    if (!feedback || !corrections || trace_length < 2 ||
        trace_length > GLIDE_MAX_TRACE || !valid_word(word, word_length))
        return false;
    position = feedback->count;
    entry.trace[trace_length] = '\0';
    entry.word[word_length] = '\0';
    memcpy(entry.trace, trace, trace_length);
    memcpy(entry.word, word, word_length);
    for (size_t i = 0; i < feedback->count; i++) {
        if (!strcmp(feedback->entries[i].trace, entry.trace) &&
            !strcmp(feedback->entries[i].word, entry.word)) {
            position = i;
            break;
        }
    }
    if (position < feedback->count) {
        memmove(&feedback->entries[position], &feedback->entries[position + 1],
                (feedback->count - position - 1) * sizeof(entry));
        feedback->count--;
    } else if (feedback->count == GLIDE_FEEDBACK_MAX) {
        memmove(&feedback->entries[0], &feedback->entries[1],
                (GLIDE_FEEDBACK_MAX - 1) * sizeof(entry));
        feedback->count--;
    }
    feedback->entries[feedback->count++] = entry;
    return true;
}

bool
glide_feedback_reject(struct glide_feedback *feedback, const char *trace,
                      size_t trace_length, const char *word,
                      size_t word_length)
{
    char normalized[GLIDE_MAX_TRACE + 1] = {0};
    size_t normalized_length;
    uint16_t corrections = 1;

    if (!feedback)
        return false;
    normalized_length = collapse(trace, trace_length, normalized,
                                 GLIDE_MAX_TRACE);
    if (normalized_length < 2 || !valid_word(word, word_length))
        return false;
    for (size_t i = 0; i < feedback->count; i++)
        if (!strcmp(feedback->entries[i].trace, normalized) &&
            strlen(feedback->entries[i].word) == word_length &&
            !memcmp(feedback->entries[i].word, word, word_length)) {
            corrections = feedback->entries[i].corrections;
            if (corrections < UINT16_MAX)
                corrections++;
            break;
        }
    return feedback_add(feedback, normalized, normalized_length, word,
                        word_length, corrections);
}

bool
glide_feedback_parse(struct glide_feedback *feedback, char *snapshot,
                     size_t length)
{
    struct glide_feedback parsed = {0};
    struct snapshot_entry {
        char algorithm[25];
        char dictionary[65];
        char trace[GLIDE_MAX_TRACE + 1];
        char word[GLIDE_MAX_WORD + 1];
    } seen[GLIDE_FEEDBACK_MAX];
    size_t entries = 0;
    char *line;
    char *end;

    if (!feedback || !snapshot || length < sizeof("feedback-v1\n") - 1 ||
        length > GLIDE_FEEDBACK_SNAPSHOT_MAX || snapshot[length] != '\0' ||
        memchr(snapshot, '\0', length)) {
        if (feedback)
            *feedback = (struct glide_feedback){0};
        return false;
    }
    line = snapshot;
    end = snapshot + length;
    if (memcmp(snapshot, "feedback-v1\n", sizeof("feedback-v1\n") - 1))
        goto invalid;
    line += sizeof("feedback-v1\n") - 1;
    while (line < end) {
        char *newline = memchr(line, '\n', (size_t)(end - line));
        char *fields[5] = {line};
        char *field;
        char *count_text;
        char *count_end;
        unsigned long count;

        if (!newline || entries == GLIDE_FEEDBACK_MAX)
            goto invalid;
        *newline = '\0';
        field = line;
        for (size_t i = 1; i < 5; i++) {
            field = strchr(field, '\t');
            if (!field)
                goto invalid;
            *field++ = '\0';
            fields[i] = field;
        }
        if (strchr(fields[4], '\t') || !valid_trace(fields[2], strlen(fields[2])) ||
            !valid_word(fields[3], strlen(fields[3])) ||
            !valid_hex(fields[1], 64) || strlen(fields[0]) < 1 ||
            strlen(fields[0]) > 24)
            goto invalid;
        for (size_t i = 0; fields[0][i]; i++)
            if (!((fields[0][i] >= 'a' && fields[0][i] <= 'z') ||
                  (fields[0][i] >= '0' && fields[0][i] <= '9') ||
                  fields[0][i] == '-'))
                goto invalid;
        if (collapse(fields[2], strlen(fields[2]),
                     (char[GLIDE_MAX_TRACE + 1]){0}, GLIDE_MAX_TRACE) !=
            strlen(fields[2]))
            goto invalid;
        errno = 0;
        count_text = fields[4];
        for (size_t i = 0; count_text[i]; i++)
            if (count_text[i] < '0' || count_text[i] > '9')
                goto invalid;
        count = strtoul(count_text, &count_end, 10);
        if (errno || *count_end || count == 0 || count > UINT16_MAX ||
            count_text[0] == '0' || (count_text[0] == '+' ||
                                     count_text[0] == '-'))
            goto invalid;
        for (size_t i = 0; i < entries; i++)
            if (!strcmp(seen[i].algorithm, fields[0]) &&
                !strcmp(seen[i].dictionary, fields[1]) &&
                !strcmp(seen[i].trace, fields[2]) &&
                !strcmp(seen[i].word, fields[3]))
                goto invalid;
        snprintf(seen[entries].algorithm, sizeof(seen[entries].algorithm), "%s",
                 fields[0]);
        snprintf(seen[entries].dictionary, sizeof(seen[entries].dictionary), "%s",
                 fields[1]);
        snprintf(seen[entries].trace, sizeof(seen[entries].trace), "%s", fields[2]);
        snprintf(seen[entries].word, sizeof(seen[entries].word), "%s", fields[3]);
        if (!strcmp(fields[0], glide_algorithm) &&
            !strcmp(fields[1], glide_dictionary_sha256) &&
            !feedback_add(&parsed, fields[2], strlen(fields[2]), fields[3],
                          strlen(fields[3]), (uint16_t)count))
            goto invalid;
        entries++;
        line = newline + 1;
    }
    *feedback = parsed;
    return true;

invalid:
    *feedback = (struct glide_feedback){0};
    return false;
}

static uint16_t
feedback_corrections(const struct glide_feedback *feedback,
                     const char *normalized_trace, const char *word,
                     size_t word_length)
{
    if (!feedback)
        return 0;
    for (size_t i = 0; i < feedback->count; i++)
        if (!strcmp(feedback->entries[i].trace, normalized_trace) &&
            strlen(feedback->entries[i].word) == word_length &&
            !memcmp(feedback->entries[i].word, word, word_length))
            return feedback->entries[i].corrections;
    return 0;
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
           const char *word, size_t length, uint64_t error, uint64_t rank_error)
{
    size_t position = 0;

    while (position < *count && matches[position].error <= rank_error) {
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
        .error = rank_error,
    };
}

void
glide_recognize(const char *trace, const struct glide_point *points,
                size_t length, const struct glide_geometry *geometry,
                struct glide_result *result)
{
    glide_recognize_with_feedback(trace, points, length, geometry, NULL, result);
}

void
glide_recognize_with_feedback(
    const char *trace, const struct glide_point *points, size_t length,
    const struct glide_geometry *geometry, const struct glide_feedback *feedback,
    struct glide_result *result)
{
    struct glide_point gesture[GLIDE_PATH_SAMPLES];
    const struct glide_bucket_range *bucket;
    struct ranked_match matches[GLIDE_MAX_MATCHES] = {0};
    size_t match_count = 0;
    uint64_t threshold;
    char normalized_trace[GLIDE_MAX_TRACE + 1] = {0};
    size_t normalized_length;

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
    normalized_length =
        collapse(trace, length, normalized_trace, GLIDE_MAX_TRACE);
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
        uint64_t rank_error;
        uint64_t penalty;

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
                penalty = normalized_length < 2
                              ? 0
                              : (uint64_t)feedback_corrections(
                                    feedback, normalized_trace,
                                    (const char *)word, word_size);
                penalty = penalty > UINT64_MAX / (threshold + 1)
                              ? UINT64_MAX
                              : penalty * (threshold + 1);
                rank_error = UINT64_MAX - error < penalty ? UINT64_MAX
                                                            : error + penalty;
                keep_match(matches, &match_count, (const char *)word, word_size,
                           error, rank_error);
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
