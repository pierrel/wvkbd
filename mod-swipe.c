#include <linux/input-event-codes.h>
#include <stddef.h>
#include <string.h>

#include "mod-swipe.h"

static uint64_t
magnitude(int64_t value)
{
    return value < 0 ? (uint64_t)-value : (uint64_t)value;
}

static void
reset(struct mod_swipe_state *state)
{
    *state = (struct mod_swipe_state){0};
}

static void
append_letter(struct mod_swipe_state *state, struct key *key, char letter,
              int32_t x, int32_t y)
{
    if (letter < 'a' || letter > 'z' || state->invalid) {
        return;
    }
    if (state->trace_length &&
        state->trace[state->trace_length - 1] == letter) {
        state->trace_points[state->trace_length - 1] =
            (struct glide_point){.x = x, .y = y};
        return;
    }
    if (state->trace_length == GLIDE_MAX_TRACE) {
        state->invalid = true;
        return;
    }
    state->trace[state->trace_length++] = letter;
    state->trace_points[state->trace_length - 1] =
        (struct glide_point){.x = x, .y = y};
    state->trace_keys[state->trace_length - 1] = key;
    state->trace_changed = true;
}

static void
maybe_enter_glide(struct mod_swipe_state *state)
{
    uint64_t span = magnitude((int64_t)state->max_x - state->min_x);

    if (!state->glide_capable || state->invalid ||
        state->action == ModSwipeGlide || state->action == ModSwipeCancelled ||
        state->trace_length < 2 ||
        state->travel < 2 * (uint64_t)state->threshold ||
        span < state->threshold) {
        return;
    }
    state->action = ModSwipeGlide;
    state->entered_glide = true;
}

bool
mod_swipe_keycode_is_character(uint32_t keycode)
{
    if (keycode < KEY_1 || keycode > KEY_SLASH) {
        return false;
    }
    switch (keycode) {
    case KEY_BACKSPACE:
    case KEY_TAB:
    case KEY_ENTER:
    case KEY_LEFTCTRL:
    case KEY_LEFTSHIFT:
        return false;
    default:
        return true;
    }
}

uint32_t
mod_swipe_threshold(uint32_t key_height)
{
    uint64_t scaled = ((uint64_t)key_height * 2) / 5;
    return scaled < 12 ? 12 : (uint32_t)scaled;
}

bool
mod_swipe_begin(struct mod_swipe_state *state, int32_t touch_id, int32_t x,
                int32_t y, uint32_t time, struct key *key, uint32_t key_height,
                bool deferred, char start_letter)
{
    if (state->active) {
        return false;
    }
    state->active = true;
    state->deferred = deferred;
    state->touch_id = touch_id;
    state->start_x = x;
    state->start_y = y;
    state->last_x = x;
    state->last_y = y;
    state->min_x = x;
    state->max_x = x;
    state->threshold = mod_swipe_threshold(key_height);
    state->last_time = time;
    state->action = ModSwipePending;
    state->key = key;
    state->glide_capable = start_letter >= 'a' && start_letter <= 'z';
    state->endpoint_mapped = state->glide_capable;
    if (state->glide_capable) {
        state->trace[0] = start_letter;
        state->trace_points[0] = (struct glide_point){.x = x, .y = y};
        state->trace_keys[0] = key;
        state->trace_length = 1;
    }
    return true;
}

bool
mod_swipe_owns(const struct mod_swipe_state *state, int32_t touch_id)
{
    return state->active && state->touch_id == touch_id;
}

bool
mod_swipe_update(struct mod_swipe_state *state, int32_t touch_id, int32_t x,
                 int32_t y, uint32_t time, struct key *key, char letter)
{
    int64_t dx;
    int64_t dy;
    uint64_t horizontal;
    uint64_t vertical;
    enum mod_swipe_action previous;

    if (!mod_swipe_owns(state, touch_id)) {
        return false;
    }
    state->last_time = time;
    if (!state->deferred || state->action == ModSwipeCancelled) {
        return false;
    }
    state->trace_changed = false;
    state->entered_glide = false;
    previous = state->action;
    if (state->motions >= MOD_SWIPE_MAX_MOTIONS) {
        state->invalid = true;
        return false;
    }
    state->motions++;
    state->travel += magnitude((int64_t)x - state->last_x) +
                     magnitude((int64_t)y - state->last_y);
    state->last_x = x;
    state->last_y = y;
    if (x < state->min_x)
        state->min_x = x;
    if (x > state->max_x)
        state->max_x = x;
    state->endpoint_mapped = key && letter >= 'a' && letter <= 'z';
    append_letter(state, key, letter, x, y);
    if (state->action == ModSwipeGlide) {
        return state->trace_changed;
    }
    dx = (int64_t)x - state->start_x;
    dy = (int64_t)y - state->start_y;
    horizontal = magnitude(dx);
    vertical = magnitude(dy);
    if (state->action == ModSwipePending &&
        (horizontal >= state->threshold || vertical >= state->threshold)) {
        if (vertical >= state->threshold && vertical >= horizontal * 2) {
            state->action =
                dy < 0 ? ModSwipeControlCandidate : ModSwipeAltCandidate;
        } else {
            state->action = state->glide_capable ? ModSwipeGlideCandidate
                                                 : ModSwipeCancelled;
        }
    }
    maybe_enter_glide(state);
    return state->action != previous || state->trace_changed;
}

static void
finish(struct mod_swipe_state *state, uint32_t time,
       struct mod_swipe_result *result)
{
    result->deferred = state->deferred;
    result->time = time;
    result->action = state->action;
    result->key = state->key;
    memcpy(result->trace, state->trace, state->trace_length);
    memcpy(result->trace_points, state->trace_points,
           state->trace_length * sizeof(state->trace_points[0]));
    result->trace_length = state->trace_length;
    result->endpoint_mapped = state->endpoint_mapped;
    result->invalid = state->invalid;
    reset(state);
}

bool
mod_swipe_finish(struct mod_swipe_state *state, int32_t touch_id, uint32_t time,
                 struct mod_swipe_result *result)
{
    if (!mod_swipe_owns(state, touch_id)) {
        return false;
    }
    finish(state, time, result);
    return true;
}

bool
mod_swipe_cancel(struct mod_swipe_state *state, struct mod_swipe_result *result)
{
    if (!state->active) {
        return false;
    }
    finish(state, state->last_time, result);
    return true;
}
