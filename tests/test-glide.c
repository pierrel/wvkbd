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
    struct glide_match match;
    size_t length = strlen(trace);

    for (size_t i = 0; i < length; i++) {
        points[i] = current.letters[trace[i] - 'a'];
    }
    assert(glide_recognize(trace, points, length, &current, &match));
    assert(match.length == strlen(word));
    assert(memcmp(match.word, word, match.length) == 0);
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
    struct glide_match match;

    expect("helo", "hello");
    memset(too_long, 'a', sizeof(too_long));
    assert(sizeof(longest_trace) - 1 == GLIDE_MAX_TRACE);
    expect(longest_trace, "hello");
    expect("area", "area");
    assert(!glide_recognize("", points, 0, &current, &match));
    assert(!glide_recognize("a-", points, 2, &current, &match));
    assert(!glide_recognize(malformed, points, sizeof(malformed), &current,
                            &match));
    assert(!glide_recognize(too_long, points, sizeof(too_long), &current,
                            &match));
    current.complete = false;
    assert(!glide_recognize("helo", points, 4, &current, &match));
    current.complete = true;
    assert(!glide_recognize("helo", points, 4, &current, &match));
    puts("glide recognizer tests passed");
    return 0;
}
