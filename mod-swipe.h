#ifndef MOD_SWIPE_H
#define MOD_SWIPE_H

#include <stdbool.h>
#include <stdint.h>

struct key;

enum mod_swipe_action {
    ModSwipeNone = 0,
    ModSwipePending,
    ModSwipeControl,
    ModSwipeAlt,
    ModSwipeCancelled,
};

struct mod_swipe_state {
    bool active;
    bool deferred;
    int32_t touch_id;
    int32_t start_x;
    int32_t start_y;
    uint32_t threshold;
    uint32_t last_time;
    enum mod_swipe_action action;
    struct key *key;
};

struct mod_swipe_result {
    bool deferred;
    uint32_t time;
    enum mod_swipe_action action;
    struct key *key;
};

bool mod_swipe_keycode_is_character(uint32_t keycode);
uint32_t mod_swipe_threshold(uint32_t key_height);
bool mod_swipe_begin(struct mod_swipe_state *state, int32_t touch_id, int32_t x,
                     int32_t y, uint32_t time, struct key *key,
                     uint32_t key_height, bool deferred);
bool mod_swipe_owns(const struct mod_swipe_state *state, int32_t touch_id);
bool mod_swipe_update(struct mod_swipe_state *state, int32_t touch_id,
                      int32_t x, int32_t y, uint32_t time);
bool mod_swipe_finish(struct mod_swipe_state *state, int32_t touch_id,
                      uint32_t time, struct mod_swipe_result *result);
bool mod_swipe_cancel(struct mod_swipe_state *state,
                      struct mod_swipe_result *result);

#endif
