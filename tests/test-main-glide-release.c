#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client-core.h>

#define main wvkbd_program_main
#include "../main.c"
#undef main

static unsigned int emitted_words;
static unsigned int candidate_commits;
static unsigned int candidate_clears;
static enum kbd_candidate_event candidate_event_result;
static unsigned int activated_keys;
static struct key *activated_key;
static uint8_t activated_mods;
static bool activation_saw_input_owner;
static bool activation_saw_glide_undo;
static unsigned int key_releases;
static unsigned int release_calls;
static unsigned int unpress_calls;
static unsigned int key_presses;
static unsigned int key_motions;
static unsigned int candidate_pointer_motions;
static int32_t candidate_pointer_x;
static int32_t candidate_pointer_y;
static int32_t candidate_pointer_button_x;
static int32_t candidate_pointer_button_y;
static unsigned int followup_calls;
static bool followup_consumed;
static const struct key *followup_key;
static uint8_t followup_mods;
static unsigned int layout_switches;
static bool layout_switch_saw_input_owner;
static unsigned int layout_draws;
static unsigned int popup_clears;
static unsigned int surface_flips;
static unsigned int surface_resizes;
static unsigned int keyboard_resizes;
static uint32_t resized_width;
static uint32_t resized_height;
static double resized_scale;
static unsigned int next_layers;
static unsigned int feedback_clears;
static unsigned int popup_feedbacks;
static unsigned int modifier_resets;
static bool popup_visible;
static struct key *feedback_key;
static char feedback_prefix[8];
static struct key *next_key;
static struct key *next_key_after_cancel;
static struct key *looked_up_key;
static bool lookup_depends_on_compose;
static bool cancel_finished;
static bool next_key_changes_interpretation;
static struct layout *switched_layout;
static size_t switched_layer_index;
static char draw_events[16];
static size_t draw_event_count;
static char configure_events[16];
static size_t configure_event_count;
static bool track_configure_order;
static enum layout_id test_layers[] = {Index, NumLayouts};

static void seed_deferred_glide(void);
static void expect_cancelled(unsigned int extra_flips);

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
    if (keyboard.vkbd && proxy == (struct wl_proxy *)keyboard.vkbd &&
        opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS) {
        modifier_resets++;
    }
    if (track_configure_order &&
        opcode == ZWLR_LAYER_SURFACE_V1_ACK_CONFIGURE) {
        configure_events[configure_event_count++] = 'A';
    }
    (void)proxy;
    (void)opcode;
    (void)interface;
    (void)version;
    (void)flags;
    return NULL;
}

void
__wrap_wl_proxy_destroy(struct wl_proxy *proxy)
{
    (void)proxy;
}

int
__wrap_wl_proxy_add_listener(struct wl_proxy *proxy,
                             void (**implementation)(void), void *data)
{
    (void)proxy;
    (void)implementation;
    (void)data;
    return 0;
}

void
kbd_release_key(struct kbd *kb, uint32_t time)
{
    (void)time;
    release_calls++;
    cancel_finished = true;
    if (kb->last_press) {
        key_releases++;
        kb->last_press = NULL;
        if (kb->compose >= 2) {
            kb->compose = 0;
            kbd_switch_layout(kb, kb->prevlayout, kb->prev_layer_index);
        }
    }
}

void
kbd_activate_key(struct kbd *kb, struct key *key, uint32_t time,
                 uint8_t transient_modifier)
{
    uint8_t saved_mods = kb->mods;

    kb->mods |= transient_modifier;
    activated_keys++;
    activated_key = key;
    activated_mods = transient_modifier;
    activation_saw_input_owner |=
        cur_press || kb->last_press || mod_swipe.active;
    activation_saw_glide_undo |= kb->glide_undo_count != 0;
    kbd_press_key(kb, key, time);
    kbd_release_key(kb, time);
    kb->mods = saved_mods;
}

bool
kbd_commit_glide_result(struct kbd *kb, const struct glide_result *result,
                        const char *trace, size_t trace_length, uint32_t time)
{
    (void)time;
    (void)trace;
    (void)trace_length;
    candidate_commits++;
    if (!result || !result->count) {
        return false;
    }
    emitted_words++;
    kb->glide_undo_count = (uint8_t)(result->matches[0].length + 1);
    return true;
}

enum kbd_candidate_event
kbd_candidate_touch_down(struct kbd *kb, int32_t id, int32_t x, int32_t y)
{
    (void)kb;
    (void)id;
    (void)x;
    (void)y;
    return candidate_event_result;
}

enum kbd_candidate_event
kbd_candidate_touch_motion(struct kbd *kb, int32_t id, int32_t x, int32_t y)
{
    (void)kb;
    (void)id;
    (void)x;
    (void)y;
    return candidate_event_result;
}

enum kbd_candidate_event
kbd_candidate_touch_up(struct kbd *kb, int32_t id, uint32_t time)
{
    (void)kb;
    (void)id;
    (void)time;
    return candidate_event_result;
}

enum kbd_candidate_event
kbd_candidate_pointer_button(struct kbd *kb, uint32_t button, bool pressed,
                             int32_t x, int32_t y, uint32_t time)
{
    (void)kb;
    (void)button;
    (void)pressed;
    candidate_pointer_button_x = x;
    candidate_pointer_button_y = y;
    (void)time;
    return candidate_event_result;
}

enum kbd_candidate_event
kbd_candidate_pointer_motion(struct kbd *kb, int32_t x, int32_t y)
{
    (void)kb;
    candidate_pointer_motions++;
    candidate_pointer_x = x;
    candidate_pointer_y = y;
    return candidate_event_result;
}

void
kbd_clear_candidates(struct kbd *kb)
{
    (void)kb;
    candidate_clears++;
}

void
kbd_show_learning_choices(struct kbd *kb)
{
    (void)kb;
}

void
kbd_clear_glide_undo(struct kbd *kb)
{
    kb->glide_undo_count = 0;
}

bool
kbd_begin_glide_followup(struct kbd *kb, const struct key *key, uint32_t time)
{
    (void)time;
    followup_calls++;
    followup_key = key;
    followup_mods = kb->mods;
    kb->glide_undo_count = 0;
    return followup_consumed;
}

void
kbd_draw_layout(struct kbd *kb)
{
    (void)kb;
    layout_draws++;
    draw_events[draw_event_count++] = 'L';
}

void
kbd_clear_last_popup(struct kbd *kb)
{
    popup_clears++;
    popup_visible = false;
    feedback_key = NULL;
    feedback_prefix[0] = '\0';
    kb->last_popup_w = kb->last_popup_h = 0;
    draw_events[draw_event_count++] = 'C';
}

void
drwsurf_flip(struct drwsurf *surface)
{
    (void)surface;
    surface_flips++;
    draw_events[draw_event_count++] = 'F';
    if (track_configure_order) {
        configure_events[configure_event_count++] = 'F';
    }
}

void
drwsurf_resize(struct drwsurf *surface, uint32_t width, uint32_t height,
               double scale)
{
    (void)surface;
    surface_resizes++;
    resized_width = width;
    resized_height = height;
    resized_scale = scale;
}

void
drwsurf_reset(struct drwsurf *surface)
{
    surface->buf = NULL;
    surface->pool_data = NULL;
    surface->width = surface->height = surface->size = 0;
    surface->scale = 0;
}

void
drw_fill_rectangle(struct drwsurf *surface, Color color, uint32_t x,
                   uint32_t y, uint32_t width, uint32_t height, int rounding)
{
    (void)surface;
    (void)color;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)rounding;
}

void
draw_inset(struct drwsurf *surface, uint32_t x, uint32_t y, uint32_t width,
           uint32_t height, uint32_t border, Color color, int rounding)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)border;
    (void)color;
    (void)rounding;
}

void
drw_draw_text(struct drwsurf *surface, Color color, uint32_t x, uint32_t y,
              uint32_t width, uint32_t height, uint32_t border,
              const char *label, PangoFontDescription *font_description)
{
    (void)surface;
    (void)color;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)border;
    (void)label;
    (void)font_description;
}

void
kbd_next_layer(struct kbd *kb, struct key *key, bool invert)
{
    (void)kb;
    (void)key;
    (void)invert;
    next_layers++;
}

void
kbd_unpress_key(struct kbd *kb, uint32_t time)
{
    (void)time;
    unpress_calls++;
    if (kb->last_press) {
        key_releases++;
        kb->last_press = NULL;
    }
}
struct key *
kbd_get_key(struct kbd *kb, uint32_t x, uint32_t y)
{
    (void)kb;
    (void)x;
    (void)y;
    looked_up_key = lookup_depends_on_compose && cancel_finished
                        ? next_key_after_cancel
                        : next_key;
    return looked_up_key;
}
bool
kbd_glide_letter(struct kbd *kb, const struct key *key, char *letter)
{
    (void)kb;
    (void)key;
    (void)letter;
    return false;
}
bool
kbd_glide_geometry(const struct kbd *kb, struct glide_geometry *geometry)
{
    (void)kb;
    *geometry = (struct glide_geometry){.key_height = 100, .complete = true};
    for (size_t i = 0; i < 26; i++) {
        geometry->letters[i] = (struct glide_point){.x = (int32_t)(i * 100)};
    }
    return true;
}
bool
kbd_key_changes_interpretation(const struct kbd *kb, const struct key *key)
{
    (void)kb;
    (void)key;
    return next_key_changes_interpretation;
}
void
kbd_show_key_feedback(struct kbd *kb, struct key *key, const char *prefix)
{
    kbd_clear_last_popup(kb);
    feedback_key = key;
    snprintf(feedback_prefix, sizeof(feedback_prefix), "%s",
             prefix ? prefix : "");
}
void
kbd_show_popup_feedback(struct kbd *kb, struct key *key, const char *label)
{
    (void)key;
    assert(strcmp(label, "?") == 0);
    popup_feedbacks++;
    popup_visible = true;
    kb->last_popup_w = kb->last_popup_h = 1;
}
void
kbd_clear_key_feedback(struct kbd *kb, struct key *key)
{
    (void)kb;
    (void)key;
    feedback_clears++;
    feedback_key = NULL;
    feedback_prefix[0] = '\0';
    popup_visible = false;
}
void
kbd_press_key(struct kbd *kb, struct key *key, uint32_t time)
{
    if (kbd_begin_glide_followup(kb, key, time)) {
        return;
    }
    key_presses++;
}
void
kbd_motion_key(struct kbd *kb, uint32_t time, uint32_t x, uint32_t y)
{
    (void)kb;
    (void)time;
    (void)x;
    (void)y;
    key_motions++;
}
void
kbd_draw_key(struct kbd *kb, struct key *key, enum key_draw_type type)
{
    (void)kb;
    (void)key;
    if (type == Swipe) {
        draw_events[draw_event_count++] = 'K';
    }
}
void
kbd_switch_layout(struct kbd *kb, struct layout *layout, size_t index)
{
    (void)kb;
    switched_layout = layout;
    switched_layer_index = index;
    layout_switches++;
    layout_switch_saw_input_owner = cur_press || keyboard.last_press;
}
void
kbd_resize(struct kbd *kb, struct layout *layouts, uint8_t count)
{
    (void)kb;
    (void)layouts;
    (void)count;
    keyboard_resizes++;
}

static void
reset(void)
{
    keyboard = (struct kbd){0};
    keyboard.glide_undo_count = 4;
    glide_learning = (struct glide_learning_sink){.fd = -1};
    mod_swipe = (struct mod_swipe_state){0};
    mod_swipe_enabled = true;
    emitted_words = 0;
    candidate_commits = 0;
    candidate_clears = 0;
    candidate_event_result = KbdCandidateMiss;
    activated_keys = 0;
    activated_key = NULL;
    activated_mods = NoMod;
    activation_saw_input_owner = false;
    activation_saw_glide_undo = false;
    key_releases = 0;
    release_calls = 0;
    unpress_calls = 0;
    key_presses = 0;
    key_motions = 0;
    candidate_pointer_motions = 0;
    candidate_pointer_x = 0;
    candidate_pointer_y = 0;
    candidate_pointer_button_x = 0;
    candidate_pointer_button_y = 0;
    followup_calls = 0;
    followup_consumed = false;
    followup_key = NULL;
    followup_mods = 0;
    layout_switches = 0;
    layout_switch_saw_input_owner = false;
    layout_draws = 0;
    popup_clears = 0;
    surface_flips = 0;
    surface_resizes = 0;
    keyboard_resizes = 0;
    resized_width = resized_height = 0;
    resized_scale = 0;
    next_layers = 0;
    feedback_clears = 0;
    popup_feedbacks = 0;
    modifier_resets = 0;
    popup_visible = false;
    feedback_key = NULL;
    feedback_prefix[0] = '\0';
    next_key = NULL;
    next_key_after_cancel = NULL;
    looked_up_key = NULL;
    lookup_depends_on_compose = false;
    cancel_finished = false;
    next_key_changes_interpretation = false;
    switched_layout = NULL;
    switched_layer_index = 0;
    draw_event_count = 0;
    configure_event_count = 0;
    track_configure_order = false;
    current_output = NULL;
    wl_outputs_size = 0;
    layer_surface = NULL;
    visibility_layer_surface = NULL;
    draw_surf.surf = NULL;
    draw_surf.buf = NULL;
    popup_draw_surf.surf = NULL;
    popup_draw_surf.buf = NULL;
    visibility_draw_surf.surf = NULL;
    visibility_draw_surf.buf = NULL;
    draw_surf_viewport = NULL;
    popup_draw_surf_viewport = NULL;
    visibility_draw_surf_viewport = NULL;
    wfs_visibility_draw_surf = NULL;
    popup_xdg_popup = NULL;
    popup_xdg_surface = NULL;
    popup_xdg_surface_configured = false;
    wfs_draw_surf = NULL;
    visibility = VisibilityExpanded;
    visibility_input = (struct visibility_input){0};
    visibility_configured = false;
    visibility_preferred_fractional_scale = 0;
    pointer_on_visibility = false;
    pointer_inside_visibility = false;
    keyboard_needs_configure = false;
    run_display = true;
    cur_press = false;
    cur_button = 0;
    cur_touch_id = -1;
    cur_x = cur_y = -1;
    keyboard.layouts = layouts;
    keyboard.layers = test_layers;
    keyboard.landscape_layers = test_layers;
}

static void
test_glide_undo_input_order(void)
{
    struct key backspace = {.type = Code, .code = KEY_BACKSPACE};

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    next_key = &backspace;
    followup_consumed = true;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
    assert(followup_calls == 1);
    assert(followup_key == &backspace);
    assert(keyboard.glide_undo_count == 0);
    assert(!cur_press && keyboard.last_press == NULL);
    wl_touch_up(NULL, NULL, 0, 8, 1);
    assert(key_releases == 0);
}

static void
test_deferred_punctuation_replaces_separator_on_release(void)
{
    struct key comma = {.type = Code, .code = KEY_COMMA};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &comma;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
    assert(mod_swipe.active);
    assert(keyboard.glide_undo_count == 4);
    assert(followup_calls == 0);
    wl_touch_up(NULL, NULL, 0, 8, 1);
    assert(followup_calls == 1);
    assert(followup_key == &comma);
    assert(followup_mods == 0);
    assert(keyboard.glide_undo_count == 0);
    assert(activated_keys == 1);
    assert(key_presses == 1);
}

static void
test_modified_release_disarms_glide_followup(void)
{
    struct key comma = {.type = Code, .code = KEY_COMMA};
    static const struct {
        enum mod_swipe_action action;
        uint8_t modifiers;
    } cases[] = {
        {ModSwipeControlCandidate, Ctrl},
        {ModSwipeAltCandidate, Alt},
        {ModSwipeControlAltCandidate, Ctrl | Alt},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        popup_xdg_surface_configured = true;
        next_key = &comma;
        wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
        mod_swipe.action = cases[i].action;
        wl_touch_up(NULL, NULL, 0, 8, 1);
        assert(followup_calls == 1);
        assert(followup_key == &comma);
        assert(followup_mods == cases[i].modifiers);
        assert(keyboard.glide_undo_count == 0);
        assert(activated_keys == 1);
        assert(activated_key == &comma);
        assert(activated_mods == cases[i].modifiers);
        assert(!activation_saw_input_owner);
        assert(activation_saw_glide_undo);
        assert(key_presses == 1);
    }
}

static void
test_control_alt_feedback_takeover_and_cancel(void)
{
    struct key start = {.type = Code, .code = KEY_H};
    struct key intersection = {.type = Code, .code = KEY_E};
    int keyboard_sentinel;

    reset();
    popup_xdg_surface_configured = true;
    next_key = &start;
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &start, 60, true, 'h'));
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(24), 0);
    assert(mod_swipe.action == ModSwipeControlAltCandidate);
    assert(feedback_key == &start);
    assert(strcmp(feedback_prefix, "C-M-") == 0);

    next_key = &intersection;
    wl_touch_motion(NULL, NULL, 3, 1, wl_fixed_from_int(48), 0);
    assert(mod_swipe.action == ModSwipeGlide);
    assert(feedback_key == NULL);
    assert(feedback_prefix[0] == '\0');
    assert(!popup_visible);
    assert(layout_draws == 1);

    reset();
    popup_xdg_surface_configured = true;
    next_key = &start;
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &start, 60, true, 'h'));
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(24), 0);
    assert(strcmp(feedback_prefix, "C-M-") == 0);
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&keyboard_sentinel;
    keyboard.mods = Ctrl;
    keyboard.compose = 1;
    glide_learning.pending_gesture = 7;
    wl_touch_cancel(NULL, NULL);
    assert(!mod_swipe.active);
    assert(keyboard.glide_undo_count == 0);
    assert(keyboard.mods == NoMod);
    assert(keyboard.compose == 0);
    assert(!glide_learning.pending_gesture);
    assert(modifier_resets == 1);
    assert(feedback_prefix[0] == '\0');
    assert(!popup_visible);
    assert(activated_keys == 0);
}

static void
test_touch_cancels_held_pointer_before_compose_transition(void)
{
    struct key key = {0};

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    keyboard.compose = 2;
    keyboard.last_press = &key;
    cur_press = true;
    cur_x = cur_y = 4;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
    assert(release_calls == 1);
    assert(unpress_calls == 0);
    assert(key_releases == 1);
    assert(layout_switches == 1);
    assert(!layout_switch_saw_input_owner);
    assert(!cur_press && cur_x == -1 && cur_y == -1);
    wl_pointer_button(NULL, NULL, 0, 8, 272, WL_POINTER_BUTTON_STATE_RELEASED);
    assert(release_calls == 1);
    assert(key_releases == 1);
}

static void
test_pointer_down_is_ignored_while_touch_owns_input(void)
{
    struct key key = {0};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &key;
    seed_deferred_glide();
    wl_pointer_button(NULL, NULL, 0, 2, 272, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(mod_swipe.active);
    assert(!cur_press);
    assert(release_calls == 0);
    assert(unpress_calls == 0);
    assert(key_presses == 0);
}

static void
test_touch_transition_cancels_before_activation(void)
{
    struct key old_key = {0};
    struct key transition_key = {0};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &transition_key;
    next_key_changes_interpretation = true;
    keyboard.last_press = &old_key;
    cur_press = true;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
    assert(key_releases == 1);
    assert(activated_keys == 1);
    assert(!activation_saw_input_owner);
    assert(!mod_swipe.active);
}

static void
test_lookup_follows_cancelled_compose_layout(void)
{
    struct key old_key = {0};
    struct key before = {0};
    struct key after = {0};

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    lookup_depends_on_compose = true;
    next_key = &before;
    next_key_after_cancel = &after;
    next_key_changes_interpretation = true;
    keyboard.compose = 2;
    keyboard.last_press = &old_key;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 1, 0, 0);
    assert(looked_up_key == &after);

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    lookup_depends_on_compose = true;
    next_key = &before;
    next_key_after_cancel = &after;
    next_key_changes_interpretation = true;
    keyboard.compose = 2;
    keyboard.last_press = &old_key;
    cur_x = cur_y = 0;
    wl_pointer_button(NULL, NULL, 0, 7, 272, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(looked_up_key == &after);
}

static void
test_cancelled_feedback_once(void)
{
    struct key key = {.type = Code, .code = KEY_A};

    reset();
    popup_xdg_surface_configured = true;
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 60, true, 0));
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(-24), 0);
    assert(mod_swipe.action == ModSwipeCancelled);
    assert(feedback_clears == 1);
    wl_touch_motion(NULL, NULL, 3, 1, wl_fixed_from_int(-48), 0);
    assert(feedback_clears == 1);
}

static void
test_orientation_output_and_configure(void)
{
    struct Output output = {.w = 100, .h = 200, .scale = 1};
    int keyboard_surface;
    int control_surface;
    int sentinel;

    reset();
    seed_deferred_glide();
    flip_landscape();
    expect_cancelled(0);

    reset();
    seed_deferred_glide();
    wl_outputs[0] = output;
    wl_outputs[0].data = (struct wl_output *)&sentinel;
    wl_outputs_size = 1;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)&control_surface;
    wl_surface_enter(NULL, NULL, wl_outputs[0].data);
    expect_cancelled(0);
    assert(current_output == &wl_outputs[0]);
    assert(visibility_draw_surf.surf == NULL);
    assert(visibility_layer_surface == NULL);

    reset();
    wl_outputs[0] = output;
    wl_outputs[0].data = (struct wl_output *)&sentinel;
    wl_outputs_size = 1;
    layer_surface = (struct zwlr_layer_surface_v1 *)&keyboard_surface;
    draw_surf.surf = (struct wl_surface *)&keyboard_surface;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)&control_surface;
    wl_surface_enter(NULL, draw_surf.surf, wl_outputs[0].data);
    assert(current_output == &wl_outputs[0]);
    assert(layer_surface == NULL);
    assert(draw_surf.surf == NULL);

    reset();
    wl_outputs[0] = output;
    wl_outputs[0].data = (struct wl_output *)&sentinel;
    wl_outputs_size = 1;
    visibility = VisibilityCollapsed;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)&control_surface;
    wl_surface_enter(NULL, visibility_draw_surf.surf, wl_outputs[0].data);
    assert(current_output == &wl_outputs[0]);
    assert(visibility_draw_surf.surf == NULL);
    assert(visibility_layer_surface == NULL);

    glide_learning.pending_gesture = 7;
    handle_global_remove(NULL, NULL, wl_outputs[0].name);
    assert(!glide_learning.pending_gesture);

    reset();
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    layer_surface = (struct zwlr_layer_surface_v1 *)&sentinel;
    compositor = (struct wl_compositor *)&sentinel;
    wm_base = (struct xdg_wm_base *)&sentinel;
    popup_xdg_positioner = (struct xdg_positioner *)&sentinel;
    empty_region = (struct wl_region *)&sentinel;
    draw_surf.surf = (struct wl_surface *)&sentinel;
    layer_surface_configure(NULL, layer_surface, 1, 320, 240);
    expect_cancelled(1);
    assert(!glide_learning.pending_gesture);

    reset();
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&sentinel;
    keyboard.mods = Ctrl;
    keyboard.compose = 1;
    wp_fractional_scale_preferred_scale(NULL, NULL, 120);
    expect_cancelled(0);
    assert(!glide_learning.pending_gesture);
    assert(keyboard.mods == NoMod && keyboard.compose == 0);
    assert(modifier_resets == 1);
}

static void
test_compose_dismissal(void)
{
    struct key key = {0};

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    keyboard.compose = 1;
    keyboard.last_press = &key;
    wl_touch_down(NULL, NULL, 0, 1, NULL, 1, 0, 0);
    assert(key_releases == 1);
    assert(!cur_press && cur_x == -1 && cur_y == -1);

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    keyboard.compose = 1;
    keyboard.last_press = &key;
    cur_x = cur_y = 0;
    wl_pointer_button(NULL, NULL, 0, 1, 272, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(key_releases == 1);
    assert(!cur_press && cur_x == -1 && cur_y == -1);
}

static void
test_cancel_active_input(void)
{
    struct key key = {0};

    reset();
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 30, true, 'h'));
    mod_swipe.action = ModSwipeGlide;
    cancel_active_input(9);
    assert(!mod_swipe.active);
    assert(layout_draws == 1);
    assert(popup_clears == 1);
    assert(surface_flips == 2);
    assert(key_releases == 0);

    reset();
    keyboard.last_press = &key;
    cur_press = true;
    cur_x = 10;
    cur_y = 20;
    cancel_active_input(9);
    assert(key_releases == 1);
    assert(!cur_press);
    assert(cur_x == -1 && cur_y == -1);
    cancel_active_input(10);
    assert(key_releases == 1);
}

static void
seed_deferred_glide(void)
{
    static struct key key;

    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 30, true, 'h'));
    mod_swipe.action = ModSwipeGlide;
}

static void
expect_cancelled(unsigned int extra_flips)
{
    assert(keyboard.glide_undo_count == 0);
    assert(!mod_swipe.active);
    assert(layout_draws == 1);
    assert(popup_clears == 1);
    assert(surface_flips == 2 + extra_flips);
    assert(key_releases == 0);
    assert(candidate_clears >= 1);
}

static void
test_lifecycle_boundaries(void)
{
    struct Output output = {.scale = 2};
    int pointer_sentinel;
    int touch_sentinel;

    reset();
    popup_xdg_surface_configured = true;
    popup_visible = true;
    keyboard.last_popup_w = keyboard.last_popup_h = 1;
    wl_pointer_axis(NULL, NULL, 2, WL_POINTER_AXIS_VERTICAL_SCROLL, 1);
    assert(!popup_visible);
    assert(popup_clears == 1);
    assert(surface_flips == 2);
    assert(next_layers == 1);

    reset();
    seed_deferred_glide();
    popup_xdg_surface_configured = true;
    wl_pointer_axis(NULL, NULL, 2, WL_POINTER_AXIS_VERTICAL_SCROLL, 1);
    expect_cancelled(1);
    assert(next_layers == 1);

    reset();
    seed_deferred_glide();
    display_handle_geometry(&output, NULL, 0, 0, 100, 200, 0, NULL, NULL, 0);
    expect_cancelled(0);

    reset();
    seed_deferred_glide();
    display_handle_scale(&output, NULL, 3);
    expect_cancelled(0);

    reset();
    seed_deferred_glide();
    wp_fractional_scale_preferred_scale(NULL, NULL, 180);
    expect_cancelled(0);

    reset();
    layer_surface = (struct zwlr_layer_surface_v1 *)&pointer_sentinel;
    draw_surf.buf = (struct wl_buffer *)&pointer_sentinel;
    popup_draw_surf.buf = (struct wl_buffer *)&pointer_sentinel;
    keyboard.w = 720;
    keyboard.h = 300;
    wp_fractional_scale_preferred_scale(NULL, NULL, 180);
    assert(keyboard.preferred_fractional_scale == 1.5);
    assert(keyboard.scale == 1.5);
    assert(keyboard_resizes == 1);
    assert(surface_flips == 1);

    popup_xdg_surface_configured = true;
    wp_fractional_scale_preferred_scale(NULL, NULL, 240);
    assert(keyboard.preferred_fractional_scale == 2);
    assert(keyboard.scale == 2);
    assert(keyboard_resizes == 2);
    assert(surface_flips == 3);

    wp_fractional_scale_preferred_scale(NULL, NULL, UINT32_MAX);
    assert(keyboard.preferred_fractional_scale == 2);
    assert(keyboard_resizes == 2);
    assert(surface_flips == 3);

    reset();
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&pointer_sentinel;
    keyboard.mods = Alt;
    keyboard.compose = 1;
    pointer = (struct wl_pointer *)&pointer_sentinel;
    touch = (struct wl_touch *)&touch_sentinel;
    seat_handle_capabilities(NULL, NULL, 0);
    expect_cancelled(0);
    assert(!glide_learning.pending_gesture);
    assert(keyboard.mods == NoMod && keyboard.compose == 0);
    assert(modifier_resets == 1);
    assert(pointer == NULL && touch == NULL);

    reset();
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    layer_surface = (struct zwlr_layer_surface_v1 *)&pointer_sentinel;
    draw_surf.surf = (struct wl_surface *)&pointer_sentinel;
    hide();
    expect_cancelled(0);
    assert(!glide_learning.pending_gesture);
    assert(visibility == VisibilityFullyHidden && layer_surface == NULL);

    reset();
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    layer_surface = (struct zwlr_layer_surface_v1 *)&pointer_sentinel;
    draw_surf.surf = (struct wl_surface *)&pointer_sentinel;
    layer_surface_closed(NULL, layer_surface);
    expect_cancelled(0);
    assert(!glide_learning.pending_gesture);
    assert(!run_display);
}

static void
expect_final_redraw(bool invalid, enum mod_swipe_action action,
                    const char *trace)
{
    struct key key = {0};

    reset();
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 30, true, 'h'));
    mod_swipe.action = action;
    mod_swipe.invalid = invalid;
    mod_swipe.endpoint_mapped = true;
    mod_swipe.trace_length = strlen(trace);
    memcpy(mod_swipe.trace, trace, mod_swipe.trace_length);
    for (size_t i = 0; i < mod_swipe.trace_length; i++) {
        mod_swipe.trace_points[i] =
            (struct glide_point){.x = (trace[i] - 'a') * 100};
    }
    wl_touch_up(NULL, NULL, 0, 9, 1);
    assert(layout_draws == 1);
    assert(popup_clears == 1);
    assert(surface_flips == (action == ModSwipeGlide && invalid ? 3U : 2U));
    assert(emitted_words == (action == ModSwipeGlide && !invalid));
    assert(candidate_commits == (action == ModSwipeGlide && !invalid));
    assert(popup_feedbacks == (action == ModSwipeGlide && invalid ? 1U : 0U));
    assert(activated_keys == 0);
    if (action == ModSwipeGlide && !invalid) {
        assert(keyboard.glide_undo_count > 1);
    } else {
        assert(keyboard.glide_undo_count == 0);
    }
}

static void
test_glide_no_match_feedback(void)
{
    struct key key = {.type = Code, .code = KEY_H};

    reset();
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 30, true, 'h'));
    mod_swipe.action = ModSwipeGlide;
    memcpy(mod_swipe.trace, "hro", 3);
    mod_swipe.trace_length = 3;
    /* A zero-length path cannot map to a word. */
    wl_touch_up(NULL, NULL, 0, 9, 1);
    assert(emitted_words == 0);
    assert(layout_draws == 1);
    assert(popup_clears == 1);
    assert(surface_flips == 3);
    assert(popup_feedbacks == 1);
    assert(popup_visible);

    kbd_show_key_feedback(&keyboard, &key, NULL);
    assert(!popup_visible);

    reset();
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 30, true, 'h'));
    mod_swipe.action = ModSwipeGlide;
    memcpy(mod_swipe.trace, "helo", 4);
    mod_swipe.trace_length = 4;
    for (size_t i = 0; i < mod_swipe.trace_length; i++) {
        mod_swipe.trace_points[i] =
            (struct glide_point){.x = (mod_swipe.trace[i] - 'a') * 100};
    }
    mod_swipe.endpoint_mapped = false;
    wl_touch_up(NULL, NULL, 0, 9, 1);
    assert(emitted_words == 0);
    assert(popup_feedbacks == 1);
    assert(popup_visible);

    cancel_active_input(10);
    assert(!popup_visible);
    assert(popup_clears == 2);
    assert(surface_flips == 4);
}

static void
test_invalid_release_actions(void)
{
    expect_final_redraw(true, ModSwipePending, "h");
    expect_final_redraw(true, ModSwipeControlCandidate, "h");
    expect_final_redraw(true, ModSwipeAltCandidate, "h");
    expect_final_redraw(true, ModSwipeControlAltCandidate, "h");
}

static void
test_glide_draw_order(void)
{
    struct key start = {.type = Code, .code = KEY_H};
    struct key intersection = {.type = Code, .code = KEY_E};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &intersection;
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &start, 60, true, 'h'));
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(24),
                    wl_fixed_from_int(13));
    assert(mod_swipe.action == ModSwipeGlideCandidate);
    wl_touch_motion(NULL, NULL, 3, 1, wl_fixed_from_int(48),
                    wl_fixed_from_int(13));
    assert(mod_swipe.action == ModSwipeGlide);
    assert(draw_event_count == 6);
    assert(memcmp(draw_events, "LCKKFF", draw_event_count) == 0);
}

static void
test_non_code_key_cannot_extend_glide_trace(void)
{
    struct key modifier = {.type = Mod, .code = KEY_A};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &modifier;
    seed_deferred_glide();
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(48), 0);
    assert(mod_swipe.trace_length == 1);
    assert(mod_swipe.trace[0] == 'h');
    assert(!mod_swipe.endpoint_mapped);
}

static void
test_candidate_routing_precedes_normal_input(void)
{
    struct key key = {.type = Code, .code = KEY_A};

    reset();
    popup_xdg_surface_configured = true;
    next_key = &key;
    candidate_event_result = KbdCandidateClaimed;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 41, wl_fixed_from_int(10),
                  wl_fixed_from_int(10));
    assert(looked_up_key == NULL);
    assert(key_presses == 0);
    assert(candidate_clears == 0);

    reset();
    popup_xdg_surface_configured = true;
    seed_deferred_glide();
    candidate_event_result = KbdCandidateOwned;
    wl_touch_motion(NULL, NULL, 8, 41, wl_fixed_from_int(20),
                    wl_fixed_from_int(20));
    assert(mod_swipe.active);
    assert(mod_swipe.trace_length == 1);
    wl_touch_up(NULL, NULL, 0, 9, 41);
    assert(mod_swipe.active);
    assert(candidate_commits == 0);

    reset();
    popup_xdg_surface_configured = true;
    next_key = &key;
    cur_x = cur_y = 10;
    candidate_event_result = KbdCandidateClaimed;
    wl_pointer_button(NULL, NULL, 0, 7, 272, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(looked_up_key == NULL);
    assert(key_presses == 0);
    assert(candidate_clears == 0);

    reset();
    popup_xdg_surface_configured = true;
    cur_press = true;
    candidate_event_result = KbdCandidateOwned;
    wl_pointer_motion(NULL, NULL, 7, wl_fixed_from_int(20),
                      wl_fixed_from_int(20));
    assert(key_motions == 0);

    reset();
    wl_pointer_leave(NULL, NULL, 0, NULL);
    assert(candidate_clears == 0);

    reset();
    popup_xdg_surface_configured = true;
    mod_swipe_enabled = false;
    next_key = &key;
    candidate_event_result = KbdCandidateMiss;
    wl_touch_down(NULL, NULL, 0, 7, NULL, 42, wl_fixed_from_int(10),
                  wl_fixed_from_int(100));
    assert(candidate_clears == 1);
    assert(looked_up_key == &key);
    assert(key_presses == 1);
}

static void
test_pointer_coordinates_and_button_ownership(void)
{
    struct key key = {.type = Code, .code = KEY_A};

    reset();
    wl_pointer_enter(NULL, NULL, 0, NULL, wl_fixed_from_int(17),
                     wl_fixed_from_int(23));
    assert(cur_x == 17 && cur_y == 23);
    assert(candidate_pointer_motions == 1);
    assert(candidate_pointer_x == 17 && candidate_pointer_y == 23);
    wl_pointer_leave(NULL, NULL, 0, NULL);
    assert(cur_x == -1 && cur_y == -1);
    assert(candidate_pointer_motions == 2);
    assert(candidate_pointer_x == -1 && candidate_pointer_y == -1);
    assert(candidate_clears == 0);

    reset();
    popup_xdg_surface_configured = true;
    mod_swipe_enabled = false;
    cur_x = cur_y = 10;
    candidate_event_result = KbdCandidateDismissed;
    wl_pointer_button(NULL, NULL, 0, 1, 272, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(cur_button == 272);
    assert(!cur_press);

    candidate_event_result = KbdCandidateMiss;
    next_key = &key;
    wl_pointer_button(NULL, NULL, 0, 2, 273, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(looked_up_key == NULL);
    assert(key_presses == 0);
    wl_pointer_button(NULL, NULL, 0, 3, 272, WL_POINTER_BUTTON_STATE_RELEASED);
    assert(cur_button == 0);
    assert(key_releases == 0);

    wl_pointer_button(NULL, NULL, 0, 4, 273, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(cur_button == 273);
    assert(cur_press);
    assert(key_presses == 1);
    wl_pointer_button(NULL, NULL, 0, 5, 273, WL_POINTER_BUTTON_STATE_RELEASED);
    assert(cur_button == 0);
    assert(!cur_press);
    assert(key_releases == 0);

    reset();
    popup_xdg_surface_configured = true;
    cur_x = cur_y = 10;
    cur_button = 272;
    cur_press = true;
    keyboard.last_press = &key;
    candidate_event_result = KbdCandidateDismissed;
    wl_pointer_button(NULL, NULL, 0, 6, 273, WL_POINTER_BUTTON_STATE_PRESSED);
    assert(cur_button == 272);
    assert(cur_press);
    assert(keyboard.last_press == &key);

    candidate_event_result = KbdCandidateMiss;
    wl_pointer_button(NULL, NULL, 0, 7, 273, WL_POINTER_BUTTON_STATE_RELEASED);
    assert(cur_button == 272);
    assert(cur_press);
    assert(release_calls == 0);
    wl_pointer_button(NULL, NULL, 0, 8, 272, WL_POINTER_BUTTON_STATE_RELEASED);
    assert(cur_button == 0);
    assert(!cur_press);
    assert(release_calls == 1);
    assert(key_releases == 1);
}

static void
prepare_visibility_surfaces(int *keyboard_surface, int *control_surface)
{
    layer_surface = (struct zwlr_layer_surface_v1 *)keyboard_surface;
    draw_surf.surf = (struct wl_surface *)keyboard_surface;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)control_surface;
    visibility_draw_surf.surf = (struct wl_surface *)control_surface;
    visibility_configured = true;
}

static void
test_visibility_input_matching_and_permanent_cancel(void)
{
    reset();
    assert(visibility_input_begin(VisibilityInputTouch, 7));
    assert(!visibility_input_begin(VisibilityInputTouch, 8));
    assert(!visibility_input_motion(VisibilityInputTouch, 8, false));
    assert(visibility_input.valid);
    assert(visibility_input_motion(VisibilityInputTouch, 7, false));
    assert(!visibility_input.valid);
    assert(!visibility_input_motion(VisibilityInputTouch, 7, true));
    assert(!visibility_input_finish(VisibilityInputTouch, 7, true));
    assert(visibility_input.owner == VisibilityInputNone);

    assert(visibility_input_begin(VisibilityInputPointer, 272));
    assert(!visibility_input_finish(VisibilityInputPointer, 273, true));
    assert(visibility_input.owner == VisibilityInputPointer);
    assert(visibility_input_finish(VisibilityInputPointer, 272, true));
    assert(visibility_input.owner == VisibilityInputNone);
}

static void
test_visibility_touch_routes_and_collapses(void)
{
    int keyboard_surface;
    int control_surface;
    int ordinary_surface;

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    wl_touch_down(NULL, NULL, 0, 0, visibility_draw_surf.surf, 6,
                  wl_fixed_from_double(-0.5), wl_fixed_from_int(10));
    assert(visibility_input.owner == VisibilityInputNone);
    wl_touch_down(NULL, NULL, 0, 1, visibility_draw_surf.surf, 7,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    assert(visibility_input.owner == VisibilityInputTouch);
    assert(keyboard.glide_undo_count == 0);
    assert(candidate_clears == 1);
    wl_touch_down(NULL, NULL, 0, 2, visibility_draw_surf.surf, 8,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_touch_down(NULL, NULL, 0, 2, (struct wl_surface *)&ordinary_surface, 8,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    assert(visibility_input.token == 7);
    assert(key_presses == 0);
    popup_xdg_surface_configured = true;
    cur_x = cur_y = 10;
    wl_pointer_button(NULL, NULL, 0, 2, 272,
                      WL_POINTER_BUTTON_STATE_PRESSED);
    wl_pointer_button(NULL, NULL, 0, 2, 272,
                      WL_POINTER_BUTTON_STATE_RELEASED);
    assert(visibility_input.owner == VisibilityInputTouch);
    assert(key_presses == 0);
    wl_pointer_motion(NULL, NULL, 2, wl_fixed_from_int(20),
                      wl_fixed_from_int(10));
    wl_pointer_axis(NULL, NULL, 2, 0, wl_fixed_from_int(1));
    assert(visibility_input.owner == VisibilityInputTouch);
    assert(candidate_pointer_motions == 0);
    assert(next_layers == 0);
    wl_touch_motion(NULL, NULL, 3, 7, wl_fixed_from_double(-0.5),
                    wl_fixed_from_int(10));
    assert(!visibility_input.valid);
    wl_touch_up(NULL, NULL, 0, 3, 8);
    assert(visibility_input.owner == VisibilityInputTouch);
    wl_touch_motion(NULL, NULL, 4, 7,
                    wl_fixed_from_int(VISIBILITY_CONTROL_WIDTH),
                    wl_fixed_from_int(10));
    wl_touch_motion(NULL, NULL, 5, 7, wl_fixed_from_int(10),
                    wl_fixed_from_int(10));
    wl_touch_up(NULL, NULL, 0, 6, 7);
    assert(visibility == VisibilityExpanded);
    assert(layer_surface != NULL);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    cur_touch_id = 3;
    wl_touch_down(NULL, NULL, 0, 1, visibility_draw_surf.surf, 7,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    assert(visibility_input.owner == VisibilityInputNone);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    keyboard.mods = Ctrl;
    keyboard.compose = 1;
    glide_learning.pending_gesture = 7;
    wl_touch_down(NULL, NULL, 0, 1, visibility_draw_surf.surf, 7,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_touch_up(NULL, NULL, 0, 2, 7);
    assert(visibility == VisibilityCollapsed);
    assert(layer_surface == NULL);
    assert(draw_surf.surf == NULL);
    assert(visibility_layer_surface != NULL);
    assert(visibility_draw_surf.surf != NULL);
    assert(keyboard.mods == NoMod);
    assert(keyboard.compose == 0);
    assert(!glide_learning.pending_gesture);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    seed_deferred_glide();
    wl_touch_down(NULL, NULL, 0, 1, visibility_draw_surf.surf, 7,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    assert(visibility_input.owner == VisibilityInputNone);
    assert(mod_swipe.active);
}

static void
test_rejected_control_touch_cannot_release_keyboard_touch(void)
{
    int control_surface;
    struct key key = {0};

    reset();
    mod_swipe_enabled = false;
    popup_xdg_surface_configured = true;
    next_key = &key;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    wl_touch_down(NULL, NULL, 0, 1, NULL, 1, wl_fixed_from_int(10),
                  wl_fixed_from_int(10));
    keyboard.last_press = &key;
    assert(cur_touch_id == 1);
    assert(keyboard.last_press == &key);
    wl_touch_down(NULL, NULL, 0, 2, visibility_draw_surf.surf, 2,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_touch_up(NULL, NULL, 0, 3, 2);
    assert(cur_touch_id == 1);
    assert(keyboard.last_press == &key);
    assert(release_calls == 0);
    wl_touch_up(NULL, NULL, 0, 4, 1);
    assert(cur_touch_id == -1);
    assert(keyboard.last_press == NULL);
    assert(release_calls == 1);
}

static void
test_visibility_pointer_routes_and_collapses(void)
{
    int keyboard_surface;
    int control_surface;
    int ordinary_surface;

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    wl_pointer_enter(NULL, NULL, 0, visibility_draw_surf.surf,
                     wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_pointer_button(NULL, NULL, 0, 1, 272,
                      WL_POINTER_BUTTON_STATE_PRESSED);
    assert(visibility_input.owner == VisibilityInputPointer);
    assert(candidate_pointer_motions == 0);
    wl_pointer_motion(NULL, NULL, 1, wl_fixed_from_double(-0.5),
                      wl_fixed_from_int(10));
    assert(!visibility_input.valid);
    popup_xdg_surface_configured = true;
    wl_touch_down(NULL, NULL, 0, 1, (struct wl_surface *)&ordinary_surface, 7,
                  wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_touch_motion(NULL, NULL, 1, 7, wl_fixed_from_int(20),
                    wl_fixed_from_int(10));
    wl_touch_up(NULL, NULL, 0, 1, 7);
    assert(visibility_input.owner == VisibilityInputPointer);
    assert(key_presses == 0);
    wl_pointer_leave(NULL, NULL, 0, visibility_draw_surf.surf);
    wl_pointer_enter(NULL, NULL, 0, (struct wl_surface *)&ordinary_surface,
                     wl_fixed_from_int(10), wl_fixed_from_int(10));
    assert(candidate_pointer_motions == 0);
    wl_pointer_leave(NULL, NULL, 0, (struct wl_surface *)&ordinary_surface);
    assert(candidate_pointer_motions == 0);
    wl_pointer_enter(NULL, NULL, 0, (struct wl_surface *)&ordinary_surface,
                     wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_pointer_button(NULL, NULL, 0, 2, 272,
                      WL_POINTER_BUTTON_STATE_RELEASED);
    assert(visibility == VisibilityExpanded);
    assert(visibility_input.owner == VisibilityInputNone);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    wl_pointer_enter(NULL, NULL, 0, visibility_draw_surf.surf,
                     wl_fixed_from_int(10), wl_fixed_from_int(10));
    wl_pointer_button(NULL, NULL, 0, 1, 272,
                      WL_POINTER_BUTTON_STATE_PRESSED);
    wl_pointer_button(NULL, NULL, 0, 2, 272, 2);
    assert(visibility_input.owner == VisibilityInputPointer);
    wl_pointer_button(NULL, NULL, 0, 2, 273,
                      WL_POINTER_BUTTON_STATE_RELEASED);
    assert(visibility_input.owner == VisibilityInputPointer);
    wl_pointer_button(NULL, NULL, 0, 3, 272,
                      WL_POINTER_BUTTON_STATE_RELEASED);
    assert(visibility == VisibilityCollapsed);
    assert(layer_surface == NULL);
    assert(visibility_layer_surface != NULL);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    popup_xdg_surface_configured = true;
    pointer_on_visibility = true;
    pointer_inside_visibility = true;
    cur_x = cur_y = 10;
    keyboard.candidates.owner = KbdCandidateOwnerPointer;
    candidate_event_result = KbdCandidateOwned;
    wl_pointer_button(NULL, NULL, 0, 1, 272,
                      WL_POINTER_BUTTON_STATE_RELEASED);
    assert(candidate_pointer_button_x == -1);
    assert(candidate_pointer_button_y == -1);
}

static void
test_visibility_full_hide_tears_down_every_surface(void)
{
    int keyboard_surface;
    int control_surface;

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    popup_xdg_popup = (struct xdg_popup *)&keyboard_surface;
    popup_xdg_surface = (struct xdg_surface *)&keyboard_surface;
    popup_draw_surf_viewport = (struct wp_viewport *)&keyboard_surface;
    popup_draw_surf.surf = (struct wl_surface *)&keyboard_surface;
    wfs_draw_surf = (struct wp_fractional_scale_v1 *)&keyboard_surface;
    draw_surf_viewport = (struct wp_viewport *)&keyboard_surface;
    visibility_draw_surf_viewport = (struct wp_viewport *)&control_surface;
    wfs_visibility_draw_surf =
        (struct wp_fractional_scale_v1 *)&control_surface;
    hide();
    assert(visibility == VisibilityFullyHidden);
    assert(layer_surface == NULL && draw_surf.surf == NULL);
    assert(popup_xdg_popup == NULL && popup_xdg_surface == NULL);
    assert(popup_draw_surf_viewport == NULL && popup_draw_surf.surf == NULL);
    assert(wfs_draw_surf == NULL && draw_surf_viewport == NULL);
    assert(visibility_layer_surface == NULL &&
           visibility_draw_surf.surf == NULL &&
           visibility_draw_surf_viewport == NULL &&
           wfs_visibility_draw_surf == NULL);

    reset();
    visibility = VisibilityCollapsed;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)&control_surface;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    glide_learning.pending_gesture = 7;
    toggle_visibility();
    assert(visibility == VisibilityFullyHidden);
    assert(!glide_learning.pending_gesture);
    assert(visibility_layer_surface == NULL &&
           visibility_draw_surf.surf == NULL);
}

static void
test_visibility_show_and_fractional_scale_cancel_control_input(void)
{
    int control_surface;
    int first_output;
    int current_output_sentinel;
    struct Output output;

    reset();
    visibility = VisibilityCollapsed;
    visibility_layer_surface =
        (struct zwlr_layer_surface_v1 *)&control_surface;
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    assert(visibility_input_begin(VisibilityInputTouch, 7));
    show();
    assert(visibility == VisibilityExpanded);
    assert(visibility_input.owner == VisibilityInputNone);

    reset();
    prepare_visibility_surfaces(&control_surface, &control_surface);
    seed_deferred_glide();
    glide_learning.pending_gesture = 7;
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&control_surface;
    keyboard.mods = Ctrl | Alt;
    keyboard.compose = 1;
    show();
    assert(!mod_swipe.active);
    assert(keyboard.last_press == NULL);
    assert(keyboard.glide_undo_count == 0);
    assert(!glide_learning.pending_gesture);
    assert(keyboard.mods == NoMod && keyboard.compose == 0);
    assert(modifier_resets == 1);

    visibility_configured = true;
    assert(visibility_input_begin(VisibilityInputPointer, 272));
    glide_learning.pending_gesture = 7;
    visibility_fractional_scale_preferred_scale(NULL, NULL, UINT32_MAX);
    assert(visibility_preferred_fractional_scale == 0);
    assert(visibility_input.owner == VisibilityInputNone);
    assert(!glide_learning.pending_gesture);
    assert(visibility_input_begin(VisibilityInputPointer, 272));
    glide_learning.pending_gesture = 8;
    visibility_fractional_scale_preferred_scale(NULL, NULL, 180);
    assert(visibility_preferred_fractional_scale == 1.5);
    assert(visibility_input.owner == VisibilityInputNone);
    assert(!glide_learning.pending_gesture);

    assert(visibility_input_begin(VisibilityInputPointer, 272));
    wl_touch_cancel(NULL, NULL);
    assert(visibility_input.owner == VisibilityInputPointer);

    visibility_surface_configure(NULL, NULL, 0, 0, UINT32_MAX);
    assert(visibility_configured);
    assert(resized_width == VISIBILITY_CONTROL_WIDTH);
    assert(resized_height == VISIBILITY_CONTROL_HEIGHT);

    reset();
    visibility_draw_surf.surf = (struct wl_surface *)&control_surface;
    visibility_configured = true;
    output =
        (struct Output){.scale = 1,
                        .data =
                            (struct wl_output *)&current_output_sentinel};
    current_output = &output;
    display_handle_scale(&output, output.data, 2);
    assert(resized_scale == 2);

    reset();
    wl_outputs_size = 1;
    wl_outputs[0].data = (struct wl_output *)&first_output;
    assert(selected_output() == NULL);
    current_output = &output;
    assert(selected_output() == output.data);

    reset();
    assert(visibility_input_begin(VisibilityInputPointer, 272));
    seat_handle_capabilities(NULL, NULL, 0);
    assert(visibility_input.owner == VisibilityInputNone);
    seed_deferred_glide();
    seat_handle_capabilities(NULL, NULL, 0);
    assert(!mod_swipe.active);
}

static void
test_configure_resets_all_input_state(void)
{
    int keyboard_surface;
    int control_surface;

    reset();
    layer_surface = (struct zwlr_layer_surface_v1 *)&keyboard_surface;
    draw_surf.buf = (struct wl_buffer *)&keyboard_surface;
    keyboard.w = 320;
    keyboard.h = 240;
    keyboard.scale = keyboard.preferred_scale;
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&keyboard_surface;
    keyboard.mods = Ctrl;
    keyboard.compose = 1;
    glide_learning.pending_gesture = 7;
    seed_deferred_glide();
    track_configure_order = true;
    layer_surface_configure(NULL, layer_surface, 1, 320, 240);
    track_configure_order = false;
    assert(!mod_swipe.active);
    assert(!glide_learning.pending_gesture);
    assert(keyboard.mods == NoMod && keyboard.compose == 0);
    assert(modifier_resets == 1);
    assert(layout_draws == 2);
    assert(surface_flips == 3);
    assert(configure_event_count == 4);
    assert(memcmp(configure_events, "AFFF", configure_event_count) == 0);

    reset();
    prepare_visibility_surfaces(&keyboard_surface, &control_surface);
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&keyboard_surface;
    keyboard.mods = Alt;
    keyboard.compose = 1;
    glide_learning.pending_gesture = 8;
    seed_deferred_glide();
    assert(visibility_input_begin(VisibilityInputPointer, 272));
    track_configure_order = true;
    visibility_surface_configure(NULL, visibility_layer_surface, 1,
                                 VISIBILITY_CONTROL_WIDTH,
                                 VISIBILITY_CONTROL_HEIGHT);
    track_configure_order = false;
    assert(visibility_input.owner == VisibilityInputNone);
    assert(!mod_swipe.active);
    assert(!glide_learning.pending_gesture);
    assert(keyboard.mods == NoMod && keyboard.compose == 0);
    assert(modifier_resets == 1);
    assert(configure_event_count > 1);
    assert(configure_events[0] == 'A');
}

static void
test_modifier_only_lifecycle_reset_redraws(void)
{
    int keyboard_surface;

    reset();
    layer_surface = (struct zwlr_layer_surface_v1 *)&keyboard_surface;
    draw_surf.buf = (struct wl_buffer *)&keyboard_surface;
    keyboard.vkbd = (struct zwp_virtual_keyboard_v1 *)&keyboard_surface;
    keyboard.mods = Ctrl;
    reset_input_lifecycle(1);
    assert(keyboard.mods == NoMod);
    assert(modifier_resets == 1);
    assert(layout_draws == 1);
    assert(surface_flips == 1);
}

static void
test_compose_layout_lifecycle_reset_restores_previous_layout(void)
{
    struct layout previous = {0};
    struct layout temporary = {0};
    int keyboard_surface;

    reset();
    layer_surface = (struct zwlr_layer_surface_v1 *)&keyboard_surface;
    draw_surf.buf = (struct wl_buffer *)&keyboard_surface;
    keyboard.layout = &temporary;
    keyboard.prevlayout = &previous;
    keyboard.prev_layer_index = 4;
    keyboard.last_abc_index = 0;
    keyboard.compose = 2;
    reset_input_lifecycle(1);
    assert(keyboard.compose == 0);
    assert(layout_switches == 1);
    assert(switched_layout == &previous);
    assert(switched_layer_index == 4);
    assert(layout_draws == 0);
    assert(surface_flips == 1);
}

int
main(void)
{
    expect_final_redraw(false, ModSwipeGlide, "helo");
    expect_final_redraw(true, ModSwipeGlide, "helo");
    expect_final_redraw(false, ModSwipeGlideCandidate, "helo");
    expect_final_redraw(false, ModSwipeCancelled, "helo");
    test_invalid_release_actions();
    test_glide_no_match_feedback();
    test_glide_undo_input_order();
    test_deferred_punctuation_replaces_separator_on_release();
    test_modified_release_disarms_glide_followup();
    test_control_alt_feedback_takeover_and_cancel();
    test_glide_draw_order();
    test_non_code_key_cannot_extend_glide_trace();
    test_candidate_routing_precedes_normal_input();
    test_pointer_coordinates_and_button_ownership();
    test_visibility_input_matching_and_permanent_cancel();
    test_visibility_touch_routes_and_collapses();
    test_rejected_control_touch_cannot_release_keyboard_touch();
    test_visibility_pointer_routes_and_collapses();
    test_visibility_full_hide_tears_down_every_surface();
    test_visibility_show_and_fractional_scale_cancel_control_input();
    test_configure_resets_all_input_state();
    test_modifier_only_lifecycle_reset_redraws();
    test_compose_layout_lifecycle_reset_restores_previous_layout();
    test_cancel_active_input();
    test_lifecycle_boundaries();
    test_orientation_output_and_configure();
    test_compose_dismissal();
    test_touch_cancels_held_pointer_before_compose_transition();
    test_pointer_down_is_ignored_while_touch_owns_input();
    test_touch_transition_cancels_before_activation();
    test_lookup_follows_cancelled_compose_layout();
    test_cancelled_feedback_once();
    puts("main glide release tests passed");
    return 0;
}
