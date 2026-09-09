#ifndef GLIDE_H
#define GLIDE_H

#include <stdbool.h>
#include <stddef.h>

#define GLIDE_MAX_TRACE 64
#define GLIDE_MAX_WORD 24

struct glide_match {
    const char *word;
    size_t length;
};

bool glide_recognize(const char *trace, size_t length, struct glide_match *match);

#endif
