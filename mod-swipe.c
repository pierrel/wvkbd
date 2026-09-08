#include <linux/input-event-codes.h>
#include <stddef.h>

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
                bool deferred)
{
    if (state->active) {
        return false;
    }
    state->active = true;
    state->deferred = deferred;
    state->touch_id = touch_id;
    state->start_x = x;
    state->start_y = y;
    state->threshold = mod_swipe_threshold(key_height);
    state->last_time = time;
    state->action = ModSwipePending;
    state->key = key;
    return true;
}

bool
mod_swipe_owns(const struct mod_swipe_state *state, int32_t touch_id)
{
    return state->active && state->touch_id == touch_id;
}

bool
mod_swipe_update(struct mod_swipe_state *state, int32_t touch_id, int32_t x,
                 int32_t y, uint32_t time)
{
    int64_t dx;
    int64_t dy;
    uint64_t horizontal;
    uint64_t vertical;

    if (!mod_swipe_owns(state, touch_id)) {
        return false;
    }
    state->last_time = time;
    if (!state->deferred || state->action != ModSwipePending) {
        return false;
    }
    dx = (int64_t)x - state->start_x;
    dy = (int64_t)y - state->start_y;
    horizontal = magnitude(dx);
    vertical = magnitude(dy);
    if (horizontal < state->threshold && vertical < state->threshold) {
        return false;
    }
    if (vertical >= state->threshold && vertical >= horizontal * 2) {
        state->action = dy < 0 ? ModSwipeControl : ModSwipeAlt;
    } else {
        state->action = ModSwipeCancelled;
    }
    return true;
}

static void
finish(struct mod_swipe_state *state, uint32_t time,
       struct mod_swipe_result *result)
{
    result->deferred = state->deferred;
    result->time = time;
    result->action = state->action;
    result->key = state->key;
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
