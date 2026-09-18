#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "glide-learning.h"

static const char session[] = "0123456789abcdef0123456789abcdef";

static struct glide_result
result_fixture(void)
{
    return (struct glide_result){
        .count = 3,
        .matches = {
            {.word = "alpha", .length = 5, .score = UINT64_MAX},
            {.word = "beta", .length = 4, .score = 2},
            {.word = "gamma", .length = 5, .score = 3},
        },
    };
}

int
main(void)
{
    struct glide_point points[GLIDE_MAX_TRACE];
    struct glide_geometry geometry = {.complete = true, .key_height = UINT32_MAX};
    struct glide_result result = result_fixture();
    char trace[GLIDE_MAX_TRACE];
    char output[GLIDE_LEARNING_JSON_MAX];
    size_t length;
    int sockets[2];
    int duplicate;
    struct glide_learning_sink sink;
    char received[GLIDE_LEARNING_JSON_MAX];
    ssize_t received_length;

    memset(trace, 'z', sizeof(trace));
    for (size_t i = 0; i < GLIDE_MAX_TRACE; i++)
        points[i] = (struct glide_point){.x = INT32_MIN, .y = INT32_MAX};
    for (size_t i = 0; i < 26; i++)
        geometry.letters[i] =
            (struct glide_point){.x = INT32_MAX, .y = INT32_MIN};
    length = glide_learning_encode_gesture(session, UINT64_MAX, trace, points,
                                           sizeof(trace), &geometry, &result,
                                           output);
    assert(length > 0 && length < sizeof(output));
    assert(memmem(output, length, "\"algorithm\":\"geometry-feedback-v1\"",
                  strlen("\"algorithm\":\"geometry-feedback-v1\"")));
    assert(memmem(output, length, "18446744073709551615", 20));
    assert(memmem(output, length, "\"rank\":3", 8));
    result.count = 0;
    assert(glide_learning_encode_gesture(session, 1, trace, points, sizeof(trace),
                                         &geometry, &result, output) > 0);
    assert(glide_learning_encode_resolution(
               session, 1, GLIDE_LEARNING_ALTERNATE_SELECTED, 3, output) > 0);
    assert(glide_learning_encode_resolution(
               session, 1, GLIDE_LEARNING_ALTERNATE_SELECTED, 1, output) == 0);

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    duplicate = dup(sockets[0]);
    assert(duplicate > 3);
    glide_learning_sink_init(&sink, duplicate);
    assert(sink.fd == -1);
    close(sockets[0]);
    close(sockets[1]);

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    assert(sockets[0] == 3);
    length = (size_t)snprintf(
        output, sizeof(output), "feedback-v1\n%s\t%s\tab\tbeta\t65535\n",
        glide_algorithm, glide_dictionary_sha256);
    assert(length < sizeof(output));
    assert(send(sockets[1], output, length, 0) == (ssize_t)length);
    glide_learning_sink_init(&sink, sockets[0]);
    assert(sink.fd == 3);
    assert(sink.feedback.count == 1);
    assert(sink.feedback.entries[0].corrections == UINT16_MAX);
    glide_learning_reject(&sink, "aabb", 4, "beta", 4);
    assert(sink.feedback.count == 1);
    assert(sink.feedback.entries[0].corrections == UINT16_MAX);
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(memmem(received, (size_t)received_length, "\"type\":\"feedback\"",
                  strlen("\"type\":\"feedback\"")));
    assert(!memmem(received, (size_t)received_length, "reason", 6));
    result = result_fixture();
    assert(glide_learning_observe(&sink, "abc", points, 3, &geometry, &result));
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(memmem(received, (size_t)received_length, "\"gesture\":1", 11));
    glide_learning_resolve(&sink, GLIDE_LEARNING_ALTERNATE_SELECTED, 2);
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(memmem(received, (size_t)received_length,
                  "\"outcome\":\"alternate-selected\",\"selected_rank\":2",
                  strlen("\"outcome\":\"alternate-selected\",\"selected_rank\":2")));
    assert(!sink.pending_gesture);

    result.count = 0;
    assert(glide_learning_observe(&sink, "abc", points, 3, &geometry, &result));
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    glide_learning_mark_retracted(&sink);
    assert(!sink.correction_pending);
    glide_learning_resolve(&sink, GLIDE_LEARNING_TOP_COMMITTED, 0);
    assert(sink.pending_gesture);
    glide_learning_resolve(&sink, GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE, 0);
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(!sink.pending_gesture);

    sink.next_gesture = UINT64_MAX;
    assert(glide_learning_observe(&sink, "abc", points, 3, &geometry, &result));
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(memmem(received, (size_t)received_length,
                  "\"gesture\":18446744073709551615",
                  strlen("\"gesture\":18446744073709551615")));
    assert(sink.pending_gesture == UINT64_MAX);
    assert(sink.next_gesture == 0);
    glide_learning_resolve(&sink, GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE, 0);
    received_length = recv(sockets[1], received, sizeof(received), 0);
    assert(received_length > 0);
    assert(!sink.pending_gesture);
    assert(!glide_learning_observe(&sink, "abc", points, 3, &geometry, &result));
    assert(sink.fd == -1);
    close(sockets[1]);
    assert(!glide_learning_observe(&sink, "abc", points, 3, &geometry, &result));
    assert(sink.fd == -1);
    puts("glide learning tests passed");
    return 0;
}
