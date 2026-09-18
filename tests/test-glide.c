#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "glide.h"

static struct glide_geometry
geometry(void)
{
    struct glide_geometry result = {.key_height = 100, .complete = true};

    for (size_t i = 0; i < 26; i++) {
        result.letters[i] = (struct glide_point){.x = (int32_t)i * 100,
                                                 .y = (int32_t)(i % 3) * 100};
    }
    return result;
}

static void
expect(const char *trace, const char *word)
{
    struct glide_geometry current = geometry();
    struct glide_point points[GLIDE_MAX_TRACE];
    struct glide_result result;
    size_t length = strlen(trace);

    for (size_t i = 0; i < length; i++) {
        points[i] = current.letters[trace[i] - 'a'];
    }
    glide_recognize(trace, points, length, &current, &result);
    assert(result.count > 0 && result.count <= GLIDE_MAX_MATCHES);
    assert(result.matches[0].length == strlen(word));
    assert(memcmp(result.matches[0].word, word, result.matches[0].length) == 0);
}

static void
expect_ranked(const char *trace, const char *const *words, size_t count)
{
    struct glide_geometry current = geometry();
    struct glide_point points[GLIDE_MAX_TRACE];
    struct glide_result result;
    size_t length = strlen(trace);

    current.key_height = 1;
    for (size_t i = 0; i < length; i++) {
        points[i] = current.letters[trace[i] - 'a'];
    }
    glide_recognize(trace, points, length, &current, &result);
    assert(result.count == count);
    for (size_t i = 0; i < count; i++) {
        assert(result.matches[i].length == strlen(words[i]));
        assert(memcmp(result.matches[i].word, words[i],
                      result.matches[i].length) == 0);
    }
}

static void
test_feedback_snapshots(void)
{
    char snapshot[GLIDE_FEEDBACK_SNAPSHOT_MAX + 1];
    char trace[65];
    char algorithm[25];
    char dictionary[65];
    char word[25];
    struct glide_feedback feedback;
    size_t length = 0;

    memset(algorithm, 'a', sizeof(algorithm) - 1);
    algorithm[sizeof(algorithm) - 1] = '\0';
    memset(dictionary, 'b', sizeof(dictionary) - 1);
    dictionary[sizeof(dictionary) - 1] = '\0';
    memset(word, 'd', sizeof(word) - 1);
    word[sizeof(word) - 1] = '\0';
    length += (size_t)snprintf(snapshot + length, sizeof(snapshot) - length,
                               "feedback-v1\n");
    for (size_t i = 0; i < GLIDE_FEEDBACK_MAX; i++) {
        memset(trace, 0, sizeof(trace));
        for (size_t j = 0; j < 62; j++)
            trace[j] = j % 2 ? 'b' : 'a';
        trace[62] = (char)('c' + i / 2);
        trace[63] = i % 2 ? 'b' : 'a';
        length += (size_t)snprintf(snapshot + length, sizeof(snapshot) - length,
                                   "%s\t%s\t%s\t%s\t65535\n", algorithm,
                                   dictionary, trace, word);
    }
    assert(length == GLIDE_FEEDBACK_SNAPSHOT_MAX);
    snapshot[length] = '\0';
    assert(glide_feedback_parse(&feedback, snapshot, length));
    assert(feedback.count == 0);
    snapshot[length - 1] = '\0';
    assert(!glide_feedback_parse(&feedback, snapshot, length));
    snapshot[length - 1] = '\n';
    assert(!glide_feedback_parse(&feedback, snapshot, length - 1));
    assert(snprintf(snapshot, sizeof(snapshot),
                    "feedback-v1\n%s\t%s\tab\tbeta\t1\n%s\t%s\tab\tbeta\t2\n",
                    glide_algorithm, glide_dictionary_sha256, glide_algorithm,
                    glide_dictionary_sha256) > 0);
    assert(!glide_feedback_parse(&feedback, snapshot, strlen(snapshot)));
}

int
main(void)
{
    static const char longest_trace[] = "hhhhhhhhhhhhhhhh"
                                        "eeeeeeeeeeeeeeee"
                                        "llllllllllllllll"
                                        "oooooooooooooooo";
    static const char malformed[] = {'a', (char)0xff};
    char too_long[GLIDE_MAX_TRACE + 1];
    struct glide_geometry current = geometry();
    struct glide_point points[GLIDE_MAX_TRACE] = {0};
    struct glide_result result;
    static const char *const one[] = {"hello"};
    static const char *const two[] = {"to", "too"};
    static const char *const three[] = {"of", "off", "oof"};
    struct glide_feedback feedback = {
        .count = 1,
        .entries = {{.trace = "to", .word = "to", .corrections = 1}},
    };

    expect("helo", "hello");
    memset(too_long, 'a', sizeof(too_long));
    assert(sizeof(longest_trace) - 1 == GLIDE_MAX_TRACE);
    expect(longest_trace, "hello");
    expect("area", "area");
    expect_ranked("helo", one, 1);
    expect_ranked("to", two, 2);
    expect_ranked("of", three, 3);
    for (size_t i = 0; i < 2; i++)
        points[i] = current.letters["to"[i] - 'a'];
    glide_recognize_with_feedback("to", points, 2, &current, &feedback, &result);
    assert(result.count == 2);
    assert(!memcmp(result.matches[0].word, "too", 3));
    assert(!memcmp(result.matches[1].word, "to", 2));
    assert(result.matches[1].score == 0);
    test_feedback_snapshots();
    glide_recognize("", points, 0, &current, &result);
    assert(result.count == 0);
    glide_recognize("a-", points, 2, &current, &result);
    assert(result.count == 0);
    glide_recognize(malformed, points, sizeof(malformed), &current, &result);
    assert(result.count == 0);
    glide_recognize(too_long, points, sizeof(too_long), &current, &result);
    assert(result.count == 0);
    current.complete = false;
    glide_recognize("helo", points, 4, &current, &result);
    assert(result.count == 0);
    current.complete = true;
    glide_recognize("helo", points, 4, &current, &result);
    assert(result.count == 0);
    puts("glide recognizer tests passed");
    return 0;
}
