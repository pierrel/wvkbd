#include "glide-learning.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define GLIDE_ALGORITHM "geometry-v1"

struct writer {
    char *output;
    size_t length;
    bool valid;
};

static void
append(struct writer *writer, const char *format, ...)
{
    va_list arguments;
    int written;

    if (!writer->valid)
        return;
    va_start(arguments, format);
    written = vsnprintf(writer->output + writer->length,
                        GLIDE_LEARNING_JSON_MAX - writer->length, format,
                        arguments);
    va_end(arguments);
    if (written < 0 ||
        (size_t)written >= GLIDE_LEARNING_JSON_MAX - writer->length)
        writer->valid = false;
    else
        writer->length += (size_t)written;
}

static bool
is_hex(const char *value, size_t length)
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
is_word(const char *word, size_t length)
{
    if (!word || length < 2 || length > GLIDE_MAX_WORD)
        return false;
    for (size_t i = 0; i < length; i++)
        if (word[i] < 'a' || word[i] > 'z')
            return false;
    return true;
}

static bool
is_trace(const char *trace, size_t length)
{
    if (!trace || length < 2 || length > GLIDE_MAX_TRACE)
        return false;
    for (size_t i = 0; i < length; i++)
        if (trace[i] < 'a' || trace[i] > 'z')
            return false;
    return true;
}

size_t
glide_learning_encode_gesture(const char *session, uint64_t gesture,
                             const char *trace,
                             const struct glide_point *points,
                             size_t trace_length,
                             const struct glide_geometry *geometry,
                             const struct glide_result *result,
                             char output[GLIDE_LEARNING_JSON_MAX])
{
    struct writer writer = {.output = output, .valid = true};

    if (!output || !is_hex(session, 32) || !gesture ||
        !is_trace(trace, trace_length) || !points || !geometry ||
        !geometry->complete || !geometry->key_height || !result ||
        result->count > GLIDE_MAX_MATCHES)
        return 0;
    for (size_t i = 0; i < result->count; i++)
        if (!is_word(result->matches[i].word, result->matches[i].length))
            return 0;
    append(&writer,
           "{\"type\":\"gesture\",\"version\":1,\"session\":\"%s\","
           "\"gesture\":%" PRIu64 ",\"algorithm\":\"%s\","
           "\"dictionary\":\"%s\",\"trace\":\"%.*s\",\"points\":[",
           session, gesture, GLIDE_ALGORITHM, glide_dictionary_sha256,
           (int)trace_length, trace);
    for (size_t i = 0; i < trace_length; i++)
        append(&writer, "%s[%" PRId32 ",%" PRId32 "]", i ? "," : "",
               points[i].x, points[i].y);
    append(&writer, "],\"geometry\":[");
    for (size_t i = 0; i < 26; i++)
        append(&writer, "%s[%" PRId32 ",%" PRId32 "]", i ? "," : "",
               geometry->letters[i].x, geometry->letters[i].y);
    append(&writer, "],\"key_height\":%" PRIu32 ",\"candidates\":[",
           geometry->key_height);
    for (size_t i = 0; i < result->count; i++)
        append(&writer,
               "%s{\"word\":\"%.*s\",\"score\":%" PRIu64
               ",\"rank\":%zu}",
               i ? "," : "", (int)result->matches[i].length,
               result->matches[i].word, result->matches[i].score, i + 1);
    append(&writer, "],\"presentation\":\"%s\"}",
           result->count ? "top-queued" : "no-candidate");
    return writer.valid ? writer.length : 0;
}

static const char *
outcome_name(enum glide_learning_outcome outcome)
{
    static const char *const names[] = {
        "top-committed",
        "alternate-selected",
        "explicit-lookup-failure",
        "explicit-user-misswipe",
        "retracted-then-reswiped",
        "manual-correction-ambiguous",
    };

    return (size_t)outcome < sizeof(names) / sizeof(names[0]) ? names[outcome]
                                                              : NULL;
}

size_t
glide_learning_encode_resolution(const char *session, uint64_t gesture,
                                enum glide_learning_outcome outcome,
                                size_t selected_rank,
                                char output[GLIDE_LEARNING_JSON_MAX])
{
    struct writer writer = {.output = output, .valid = true};
    const char *name = outcome_name(outcome);

    if (!output || !is_hex(session, 32) || !gesture || !name ||
        ((outcome == GLIDE_LEARNING_ALTERNATE_SELECTED) !=
         (selected_rank >= 2 && selected_rank <= GLIDE_MAX_MATCHES)))
        return 0;
    append(&writer,
           "{\"type\":\"resolution\",\"version\":1,\"session\":\"%s\","
           "\"gesture\":%" PRIu64 ",\"outcome\":\"%s\"",
           session, gesture, name);
    if (outcome == GLIDE_LEARNING_ALTERNATE_SELECTED)
        append(&writer, ",\"selected_rank\":%zu", selected_rank);
    append(&writer, "}");
    return writer.valid ? writer.length : 0;
}

static void
disable(struct glide_learning_sink *sink)
{
    if (sink->fd >= 0)
        close(sink->fd);
    sink->fd = -1;
    sink->pending_gesture = 0;
    sink->pending_has_candidates = false;
    sink->correction_pending = false;
}

static bool
send_record(struct glide_learning_sink *sink, const char *record, size_t length)
{
    ssize_t sent;

    if (!sink || sink->fd < 0 || !record || !length)
        return false;
    sent = send(sink->fd, record, length, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent < 0 || (size_t)sent != length) {
        disable(sink);
        return false;
    }
    return true;
}

void
glide_learning_sink_init(struct glide_learning_sink *sink, int fd)
{
    int type;
    socklen_t type_length = sizeof(type);
    struct sockaddr_un local, peer;
    socklen_t local_length = sizeof(local), peer_length = sizeof(peer);
    unsigned char random[16];
    static const char hex[] = "0123456789abcdef";
    bool valid =
        fd == 3 && getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) == 0 &&
        type == SOCK_DGRAM &&
        getsockname(fd, (struct sockaddr *)&local, &local_length) == 0 &&
        getpeername(fd, (struct sockaddr *)&peer, &peer_length) == 0 &&
        local.sun_family == AF_UNIX && peer.sun_family == AF_UNIX &&
        local_length == offsetof(struct sockaddr_un, sun_path) &&
        peer_length == offsetof(struct sockaddr_un, sun_path) &&
        getrandom(random, sizeof(random), GRND_NONBLOCK) ==
            (ssize_t)sizeof(random);

    *sink = (struct glide_learning_sink){.fd = -1};
    if (!valid) {
        if (fd >= 0)
            close(fd);
        return;
    }
    sink->fd = fd;
    sink->next_gesture = 1;
    for (size_t i = 0; i < sizeof(random); i++) {
        sink->session[i * 2] = hex[random[i] >> 4];
        sink->session[i * 2 + 1] = hex[random[i] & 15];
    }
}

bool
glide_learning_observe(struct glide_learning_sink *sink, const char *trace,
                       const struct glide_point *points, size_t trace_length,
                       const struct glide_geometry *geometry,
                       const struct glide_result *result)
{
    char record[GLIDE_LEARNING_JSON_MAX];
    size_t length;
    uint64_t gesture;

    if (!sink || sink->fd < 0)
        return false;
    if (sink->next_gesture == 0) {
        disable(sink);
        return false;
    }
    gesture = sink->next_gesture++;
    length = glide_learning_encode_gesture(sink->session, gesture, trace, points,
                                           trace_length, geometry, result, record);
    if (!length || !send_record(sink, record, length))
        return false;
    sink->pending_gesture = gesture;
    sink->pending_has_candidates = result->count != 0;
    return true;
}

void
glide_learning_resolve(struct glide_learning_sink *sink,
                       enum glide_learning_outcome outcome, size_t selected_rank)
{
    char record[GLIDE_LEARNING_JSON_MAX];
    size_t length;

    if (!sink || !sink->pending_gesture ||
        (sink->pending_has_candidates &&
         (outcome == GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE ||
          outcome == GLIDE_LEARNING_EXPLICIT_USER_MISSWIPE)) ||
        (!sink->pending_has_candidates &&
         outcome != GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE &&
         outcome != GLIDE_LEARNING_EXPLICIT_USER_MISSWIPE))
        return;
    length = glide_learning_encode_resolution(sink->session, sink->pending_gesture,
                                              outcome, selected_rank, record);
    if (!length || !send_record(sink, record, length))
        return;
    sink->pending_gesture = 0;
    sink->pending_has_candidates = false;
    sink->correction_pending = false;
}

void
glide_learning_mark_retracted(struct glide_learning_sink *sink)
{
    if (sink && sink->pending_gesture && sink->pending_has_candidates)
        sink->correction_pending = true;
}

void
glide_learning_resolve_correction(struct glide_learning_sink *sink, bool reswiped)
{
    if (sink && sink->correction_pending)
        glide_learning_resolve(
            sink, reswiped ? GLIDE_LEARNING_RETRACTED_THEN_RESWIPED
                            : GLIDE_LEARNING_MANUAL_CORRECTION_AMBIGUOUS,
            0);
}

void
glide_learning_clear(struct glide_learning_sink *sink)
{
    if (sink) {
        sink->pending_gesture = 0;
        sink->pending_has_candidates = false;
        sink->correction_pending = false;
    }
}
