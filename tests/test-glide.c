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
    expect("helo", "hello");
    expect("area", "area");
    struct glide_match match;
    assert(!glide_recognize("", 0, &match));
    assert(!glide_recognize("a-", 2, &match));
    puts("glide recognizer tests passed");
    return 0;
}
