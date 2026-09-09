#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "glide.h"

static void
expect(const char *trace, const char *word)
{
    struct glide_match match;
    assert(glide_recognize(trace, strlen(trace), &match));
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
    static const char too_long[GLIDE_MAX_TRACE + 1] = {['\0'] = 'a'};

    expect("helo", "hello");
    assert(sizeof(longest_trace) - 1 == GLIDE_MAX_TRACE);
    expect(longest_trace, "hello");
    expect("area", "area");
    struct glide_match match;
    assert(!glide_recognize("", 0, &match));
    assert(!glide_recognize("a-", 2, &match));
    assert(!glide_recognize(malformed, sizeof(malformed), &match));
    assert(!glide_recognize(too_long, sizeof(too_long), &match));
    puts("glide recognizer tests passed");
    return 0;
}
