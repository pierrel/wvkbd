#include <assert.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "letters.h"
#include "mod-swipe.h"

struct key {
    int marker;
};

static struct key first_key = {1};
static struct key second_key = {2};

static void
test_explicit_letter_map(void)
{
    static const uint32_t codes[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
        KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
        KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        char letter;
        uint32_t code;
        assert(glide_letter_from_evdev(codes[i], &letter));
        assert(letter == (char)('a' + i));
        assert(glide_letter_to_evdev(letter, &code));
        assert(code == codes[i]);
    }
    assert(!glide_letter_from_evdev(KEY_SEMICOLON, NULL));
    assert(!glide_letter_from_evdev(KEY_APOSTROPHE, NULL));
    assert(!glide_letter_from_evdev(KEY_GRAVE, NULL));
    assert(!glide_letter_from_evdev(KEY_LEFTSHIFT, NULL));
    assert(!glide_letter_from_evdev(KEY_BACKSLASH, NULL));
}

static void
test_threshold_and_eligibility(void)
{
    assert(mod_swipe_threshold(0) == 12);
    assert(mod_swipe_threshold(30) == 12);
    assert(mod_swipe_threshold(60) == 24);
    assert(mod_swipe_keycode_is_character(KEY_1));
    assert(mod_swipe_keycode_is_character(KEY_EQUAL));
    assert(mod_swipe_keycode_is_character(KEY_Q));
    assert(mod_swipe_keycode_is_character(KEY_A));
    assert(mod_swipe_keycode_is_character(KEY_BACKSLASH));
    assert(mod_swipe_keycode_is_character(KEY_M));
    assert(mod_swipe_keycode_is_character(KEY_SLASH));
    assert(!mod_swipe_keycode_is_character(KEY_ESC));
    assert(!mod_swipe_keycode_is_character(KEY_BACKSPACE));
    assert(!mod_swipe_keycode_is_character(KEY_TAB));
    assert(!mod_swipe_keycode_is_character(KEY_ENTER));
    assert(!mod_swipe_keycode_is_character(KEY_LEFTCTRL));
    assert(!mod_swipe_keycode_is_character(KEY_LEFTSHIFT));
    assert(!mod_swipe_keycode_is_character(KEY_SPACE));
    assert(!mod_swipe_keycode_is_character(KEY_UP));
}

static void
test_tap_and_competing_ids(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 7, 100, 200, 10, &first_key, 60, true, 'a'));
    assert(
        !mod_swipe_begin(&state, 8, 100, 200, 11, &second_key, 60, true, 'b'));
    assert(!mod_swipe_update(&state, 8, 100, 150, 12, &second_key, 'b'));
    assert(!mod_swipe_update(&state, 7, 105, 190, 12, &first_key, 'a'));
    assert(!mod_swipe_finish(&state, 8, 13, &result));
    assert(mod_swipe_finish(&state, 7, 14, &result));
    assert(result.deferred);
    assert(result.time == 14);
    assert(result.action == ModSwipePending);
    assert(result.key == &first_key);
    assert(!state.active);
}

static void
test_control_and_alt_latch(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 1, 100, 200, 10, &first_key, 60, true, 'a'));
    assert(!mod_swipe_update(&state, 1, 111, 177, 11, &first_key, 'a'));
    assert(mod_swipe_update(&state, 1, 112, 176, 12, &first_key, 'a'));
    assert(state.action == ModSwipeControlCandidate);
    assert(!mod_swipe_update(&state, 1, 100, 260, 13, &first_key, 'a'));
    assert(state.action == ModSwipeControlCandidate);
    assert(mod_swipe_finish(&state, 1, 14, &result));
    assert(result.action == ModSwipeControlCandidate);

    assert(mod_swipe_begin(&state, 2, 100, 200, 20, &first_key, 60, true, 'a'));
    assert(mod_swipe_update(&state, 2, 100, 224, 21, &first_key, 'a'));
    assert(state.action == ModSwipeAltCandidate);
    assert(mod_swipe_finish(&state, 2, 22, &result));
    assert(result.action == ModSwipeAltCandidate);
}

static void
test_cancellation_and_signed_coordinates(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 1, 100, 200, 10, &first_key, 60, true, 0));
    assert(mod_swipe_update(&state, 1, 124, 200, 11, &first_key, 0));
    assert(state.action == ModSwipeCancelled);
    assert(mod_swipe_finish(&state, 1, 12, &result));
    assert(result.action == ModSwipeCancelled);

    assert(mod_swipe_begin(&state, 1, 100, 200, 20, &first_key, 60, true, 0));
    assert(mod_swipe_update(&state, 1, 113, 176, 21, &first_key, 0));
    assert(state.action == ModSwipeCancelled);

    assert(mod_swipe_cancel(&state, &result));
    assert(result.action == ModSwipeCancelled);
    assert(result.time == 21);
    assert(!mod_swipe_cancel(&state, &result));

    assert(mod_swipe_begin(&state, 1, INT32_MAX, INT32_MAX, 30, &first_key, 60,
                           true, 0));
    assert(
        mod_swipe_update(&state, 1, INT32_MIN, INT32_MAX, 31, &first_key, 0));
    assert(state.action == ModSwipeCancelled);
}

static void
test_passthrough_capture(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 4, 10, 20, 5, &first_key, 60, false, 0));
    assert(!mod_swipe_update(&state, 4, 10, -100, 6, &first_key, 0));
    assert(mod_swipe_cancel(&state, &result));
    assert(!result.deferred);
    assert(result.time == 6);
    assert(result.action == ModSwipePending);
}

static void
test_glide_boundaries_and_takeover(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 1, 100, 200, 0, &first_key, 60, true, 'h'));
    assert(mod_swipe_update(&state, 1, 124, 200, 1, &second_key, 'e'));
    assert(state.action == ModSwipeGlideCandidate);
    assert(state.trace_length == 2);
    assert(mod_swipe_finish(&state, 1, 2, &result));
    assert(result.action == ModSwipeGlideCandidate);

    assert(mod_swipe_begin(&state, 1, 100, 200, 0, &first_key, 60, true, 'h'));
    assert(mod_swipe_update(&state, 1, 100, 176, 1, &first_key, 'h'));
    assert(state.action == ModSwipeControlCandidate);
    assert(mod_swipe_update(&state, 1, 130, 176, 2, &second_key, 'e'));
    assert(state.action == ModSwipeGlide);
    assert(mod_swipe_finish(&state, 1, 3, &result));
    assert(result.action == ModSwipeGlide);
    assert(result.trace_length == 2);
    assert(memcmp(result.trace, "he", 2) == 0);

    assert(mod_swipe_begin(&state, 1, 100, 200, 0, &first_key, 60, true, 'h'));
    assert(mod_swipe_update(&state, 1, 100, 176, 1, &second_key, 'e'));
    assert(state.action == ModSwipeControlCandidate);
    assert(mod_swipe_update(&state, 1, 100, 128, 2, &first_key, 'h'));
    assert(state.action == ModSwipeControlCandidate);
    assert(mod_swipe_finish(&state, 1, 3, &result));
    assert(result.action == ModSwipeControlCandidate);
}

static void
test_trace_and_motion_limits(void)
{
    struct mod_swipe_state state = {0};
    struct mod_swipe_result result;

    assert(mod_swipe_begin(&state, 1, 0, 0, 0, &first_key, 30, true, 'a'));
    for (size_t i = 1; i < GLIDE_MAX_TRACE; i++) {
        char letter = (char)('a' + (i % 26));

        if (letter == state.trace[state.trace_length - 1]) {
            letter = 'z';
        }
        assert(mod_swipe_update(&state, 1, (int32_t)i * 12, 0, (uint32_t)i,
                                &second_key, letter));
    }
    assert(state.trace_length == GLIDE_MAX_TRACE);
    assert(!state.invalid);
    assert(!mod_swipe_update(&state, 1, 1000, 0, 70, &second_key, 'q'));
    assert(state.invalid);
    assert(mod_swipe_finish(&state, 1, 71, &result));
    assert(result.invalid);

    assert(mod_swipe_begin(&state, 1, 0, 0, 0, &first_key, 30, true, 'a'));
    for (uint32_t i = 0; i < MOD_SWIPE_MAX_MOTIONS; i++) {
        assert(!mod_swipe_update(&state, 1, (int32_t)(i & 1), 0, i + 1,
                                 &first_key, 'a'));
    }
    assert(state.motions == MOD_SWIPE_MAX_MOTIONS);
    assert(!state.invalid);
    assert(!mod_swipe_update(&state, 1, 0, 0, MOD_SWIPE_MAX_MOTIONS + 1,
                             &first_key, 'a'));
    assert(state.invalid);
}

static void
test_all_letters_trace(void)
{
    for (char letter = 'a'; letter <= 'z'; letter++) {
        struct mod_swipe_state state = {0};
        struct mod_swipe_result result;

        assert(mod_swipe_begin(&state, letter, 0, 0, 0, &first_key, 30, true,
                               letter));
        assert(state.glide_capable);
        assert(state.trace_length == 1);
        assert(state.trace[0] == letter);
        assert(mod_swipe_finish(&state, letter, 1, &result));
        assert(result.trace_length == 1);
        assert(result.trace[0] == letter);
    }
}

int
main(void)
{
    test_explicit_letter_map();
    test_threshold_and_eligibility();
    test_tap_and_competing_ids();
    test_control_and_alt_latch();
    test_cancellation_and_signed_coordinates();
    test_passthrough_capture();
    test_glide_boundaries_and_takeover();
    test_trace_and_motion_limits();
    test_all_letters_trace();
    puts("modifier swipe tests passed");
    return 0;
}
