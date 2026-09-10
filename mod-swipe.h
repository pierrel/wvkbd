#ifndef MOD_SWIPE_H
#define MOD_SWIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "glide.h"

struct key;

#define MOD_SWIPE_MAX_MOTIONS 2048

enum mod_swipe_action {
    ModSwipeNone = 0,
    ModSwipePending,
    ModSwipeControlCandidate,
    ModSwipeAltCandidate,
    ModSwipeGlideCandidate,
    ModSwipeGlide,
    ModSwipeCancelled,
};

struct mod_swipe_state {
    bool active;
    bool deferred;
    int32_t touch_id;
    int32_t start_x;
    int32_t start_y;
    int32_t last_x;
    int32_t last_y;
    int32_t min_x;
    int32_t max_x;
    uint32_t threshold;
    uint32_t last_time;
    uint32_t motions;
    uint64_t travel;
    bool glide_capable;
    bool endpoint_mapped;
    bool invalid;
    bool trace_changed;
    bool entered_glide;
    char trace[GLIDE_MAX_TRACE];
    struct glide_point trace_points[GLIDE_MAX_TRACE];
    struct key *trace_keys[GLIDE_MAX_TRACE];
    size_t trace_length;
    enum mod_swipe_action action;
    struct key *key;
};

struct mod_swipe_result {
    bool deferred;
    uint32_t time;
    enum mod_swipe_action action;
    struct key *key;
    char trace[GLIDE_MAX_TRACE];
    struct glide_point trace_points[GLIDE_MAX_TRACE];
    size_t trace_length;
    bool endpoint_mapped;
    bool invalid;
};

bool mod_swipe_keycode_is_character(uint32_t keycode);
uint32_t mod_swipe_threshold(uint32_t key_height);
bool mod_swipe_begin(struct mod_swipe_state *state, int32_t touch_id, int32_t x,
                     int32_t y, uint32_t time, struct key *key,
                     uint32_t key_height, bool deferred, char start_letter);
bool mod_swipe_owns(const struct mod_swipe_state *state, int32_t touch_id);
bool mod_swipe_update(struct mod_swipe_state *state, int32_t touch_id,
                      int32_t x, int32_t y, uint32_t time, struct key *key,
                      char letter);
bool mod_swipe_finish(struct mod_swipe_state *state, int32_t touch_id,
                      uint32_t time, struct mod_swipe_result *result);
bool mod_swipe_cancel(struct mod_swipe_state *state,
                      struct mod_swipe_result *result);

#endif
