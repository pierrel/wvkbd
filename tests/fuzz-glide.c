#include <stddef.h>
#include <stdint.h>

#include "glide.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct glide_match match;

    glide_recognize((const char *)data, size, &match);
    return 0;
}
