#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>
#include <wayland-client-core.h>

#include "glide.h"
#include "letters.h"
#include "keyboard.h"
#include "mod-swipe.h"

struct trace {
    char letters[GLIDE_MAX_TRACE];
    struct glide_point points[GLIDE_MAX_TRACE];
    size_t length;
    enum mod_swipe_action action;
    bool endpoint_mapped;
};

uint32_t
__wrap_wl_proxy_get_version(struct wl_proxy *proxy)
{
    (void)proxy;
    return 1;
}

struct wl_proxy *
__wrap_wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                              const struct wl_interface *interface,
                              uint32_t version, uint32_t flags, ...)
{
    (void)proxy;
    (void)opcode;
    (void)interface;
    (void)version;
    (void)flags;
    return NULL;
}

static struct key *
letter_at(struct kbd *keyboard, uint32_t x, uint32_t y, char *letter)
{
    struct key *key = kbd_get_key(keyboard, x, y);

    return key && glide_letter_from_evdev(key->code, letter) ? key : NULL;
}

static struct trace
finish_trace(struct mod_swipe_state *state, uint32_t time)
{
    struct mod_swipe_result result;
    struct trace trace = {0};

    assert(mod_swipe_finish(state, 1, time, &result));
    assert(!result.invalid);
    assert(result.trace_length <= GLIDE_MAX_TRACE);
    memcpy(trace.letters, result.trace, result.trace_length);
    memcpy(trace.points, result.trace_points,
           result.trace_length * sizeof(trace.points[0]));
    trace.length = result.trace_length;
    trace.action = result.action;
    trace.endpoint_mapped = result.endpoint_mapped;
    return trace;
}

static struct key *
key_for_letter(struct kbd *keyboard, char expected)
{
    for (struct key *key = keyboard->layout->keys; key->type != Last; key++) {
        char letter;

        if (glide_letter_from_evdev(key->code, &letter) && letter == expected) {
            return key;
        }
    }
    return NULL;
}

static struct trace
trace_word(struct kbd *keyboard, const char *word, bool unmapped_end)
{
    struct key *from = key_for_letter(keyboard, word[0]);
    struct mod_swipe_state state = {0};
    uint32_t time = 0;
    int32_t start_x;
    int32_t start_y;

    assert(from);
    start_x = (int32_t)(from->x + from->w / 2);
    start_y = (int32_t)(from->y + from->h / 2);
    assert(mod_swipe_begin(&state, 1, start_x, start_y, time++, from, from->h,
                           true, word[0]));
    for (size_t i = 1; word[i]; i++) {
        struct key *to = key_for_letter(keyboard, word[i]);
        int64_t from_x = state.trace_points[state.trace_length - 1].x;
        int64_t from_y = state.trace_points[state.trace_length - 1].y;
        int64_t to_x;
        int64_t to_y;
        int64_t dx;
        int64_t dy;
        int64_t span;
        int64_t steps;

        assert(to);
        to_x = (int64_t)to->x + to->w / 2;
        to_y = (int64_t)to->y + to->h / 2;
        dx = to_x - from_x;
        dy = to_y - from_y;
        span = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        steps = (span + 3) / 4;
        for (int64_t step = 1; step <= steps; step++) {
            int32_t x = (int32_t)(from_x + dx * step / steps);
            int32_t y = (int32_t)(from_y + dy * step / steps);
            char letter = 0;

            struct key *key =
                letter_at(keyboard, (uint32_t)x, (uint32_t)y, &letter);

            mod_swipe_update(&state, 1, x, y, time++, key, letter);
        }
    }
    if (unmapped_end) {
        assert(!mod_swipe_update(&state, 1, -1, -1, time++, NULL, 0));
    }
    return finish_trace(&state, time);
}

static struct trace
trace_letters(struct kbd *keyboard, const char *letters)
{
    struct mod_swipe_state state = {0};
    struct key *first = key_for_letter(keyboard, letters[0]);
    uint32_t time = 0;

    assert(first);
    assert(mod_swipe_begin(&state, 1, (int32_t)(first->x + first->w / 2),
                           (int32_t)(first->y + first->h / 2), time++, first,
                           first->h, true, letters[0]));
    for (size_t i = 1; letters[i]; i++) {
        struct key *key = key_for_letter(keyboard, letters[i]);

        assert(key);
        mod_swipe_update(&state, 1, (int32_t)(key->x + key->w / 2),
                         (int32_t)(key->y + key->h / 2), time++, key,
                         letters[i]);
    }
    return finish_trace(&state, time);
}

static void
expect_word(struct kbd *keyboard, const struct glide_geometry *geometry,
            const char *word, const char *expected_trace)
{
    struct trace trace = trace_word(keyboard, word, false);
    struct glide_match match;

    assert(strcmp(trace.letters, expected_trace) == 0);
    assert(trace.action == ModSwipeGlide);
    assert(trace.endpoint_mapped);
    assert(glide_recognize(trace.letters, trace.points, trace.length, geometry,
                           &match));
    assert(strlen(word) == match.length);
    assert(memcmp(word, match.word, match.length) == 0);
}

static void
test_full_geometry(void)
{
    struct kbd keyboard = {.layout = &layouts[Full]};
    struct glide_geometry geometry;

    kbd_init_layout(keyboard.layout, 720, 300);
    assert(kbd_glide_geometry(&keyboard, &geometry));
    assert(geometry.complete);
    assert(geometry.key_height > 0);
    expect_word(&keyboard, &geometry, "hello", "hgftrertyhjklo");
    expect_word(&keyboard, &geometry, "there", "tyghgftrere");
    expect_word(&keyboard, &geometry, "test", "tresdrt");
    expect_word(&keyboard, &geometry, "area", "aserewsa");
    expect_word(&keyboard, &geometry, "phone", "poikjhjiokjnbhgfre");
}

static void
test_rejects_bad_paths(struct kbd *keyboard,
                       const struct glide_geometry *geometry)
{
    struct trace trace = trace_letters(keyboard, "hjbo");
    struct glide_match match;

    assert(!glide_recognize(trace.letters, trace.points, trace.length, geometry,
                            &match));
    trace = trace_word(keyboard, "hro", false);
    assert(!glide_recognize(trace.letters, trace.points, trace.length, geometry,
                            &match));
}

static void
test_rejects_unmapped_endpoint(struct kbd *keyboard,
                               const struct glide_geometry *geometry)
{
    struct trace trace = trace_word(keyboard, "hello", true);
    struct glide_match match;

    assert(!trace.endpoint_mapped);
    assert(glide_recognize(trace.letters, trace.points, trace.length, geometry,
                           &match));
}

int
main(void)
{
    struct kbd keyboard = {.layout = &layouts[Full]};
    struct glide_geometry geometry;

    kbd_init_layout(keyboard.layout, 720, 300);
    assert(kbd_glide_geometry(&keyboard, &geometry));
    test_full_geometry();
    test_rejects_bad_paths(&keyboard, &geometry);
    test_rejects_unmapped_endpoint(&keyboard, &geometry);
    puts("glide geometry tests passed");
    return 0;
}
