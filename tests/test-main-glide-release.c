#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client-core.h>

#define main wvkbd_program_main
#include "../main.c"
#undef main

static unsigned int emitted_words;
static unsigned int activated_keys;
static bool activation_saw_input_owner;
static unsigned int key_releases;
static unsigned int release_calls;
static unsigned int unpress_calls;
static unsigned int key_presses;
static unsigned int layout_switches;
static bool layout_switch_saw_input_owner;
static unsigned int layout_draws;
static unsigned int popup_clears;
static unsigned int surface_flips;
static unsigned int next_layers;
static unsigned int feedback_clears;
static unsigned int popup_feedbacks;
static bool popup_visible;
static struct key *next_key;
static bool next_key_changes_interpretation;
static char draw_events[16];
static size_t draw_event_count;
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
    if (kb->last_press) {
        key_releases++;
        kb->last_press = NULL;
    }
}

void
kbd_activate_key(struct kbd *kb, struct key *key, uint32_t time,
                 uint8_t transient_modifier)
{
    (void)kb;
    (void)key;
    (void)time;
    (void)transient_modifier;
    activated_keys++;
    activation_saw_input_owner |= cur_press || kb->last_press || mod_swipe.active;
}

bool
kbd_emit_ascii_word(struct kbd *kb, const char *word, size_t length,
                    uint32_t time)
{
    (void)kb;
    (void)word;
    (void)length;
    (void)time;
    emitted_words++;
    return true;
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
    kb->last_popup_w = kb->last_popup_h = 0;
    draw_events[draw_event_count++] = 'C';
}

void
drwsurf_flip(struct drwsurf *surface)
{
    (void)surface;
    surface_flips++;
    draw_events[draw_event_count++] = 'F';
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
    return next_key;
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
    (void)key;
    (void)prefix;
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
}
void
kbd_press_key(struct kbd *kb, struct key *key, uint32_t time)
{
    (void)kb;
    (void)key;
    (void)time;
    key_presses++;
}
void
kbd_motion_key(struct kbd *kb, uint32_t time, uint32_t x, uint32_t y)
{
    (void)kb;
    (void)time;
    (void)x;
    (void)y;
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
    (void)layout;
    (void)index;
    layout_switches++;
    layout_switch_saw_input_owner = cur_press || keyboard.last_press;
}
void
kbd_resize(struct kbd *kb, struct layout *layouts, uint8_t count)
{
    (void)kb;
    (void)layouts;
    (void)count;
}

static void
reset(void)
{
    keyboard = (struct kbd){0};
    mod_swipe = (struct mod_swipe_state){0};
    mod_swipe_enabled = true;
    emitted_words = 0;
    activated_keys = 0;
    activation_saw_input_owner = false;
    key_releases = 0;
    release_calls = 0;
    unpress_calls = 0;
    key_presses = 0;
    layout_switches = 0;
    layout_switch_saw_input_owner = false;
    layout_draws = 0;
    popup_clears = 0;
    surface_flips = 0;
    next_layers = 0;
    feedback_clears = 0;
    popup_feedbacks = 0;
    popup_visible = false;
    next_key = NULL;
    next_key_changes_interpretation = false;
    draw_event_count = 0;
    current_output = NULL;
    wl_outputs_size = 0;
    layer_surface = NULL;
    hidden = false;
    run_display = true;
    cur_press = false;
    cur_x = cur_y = -1;
    keyboard.layouts = layouts;
    keyboard.layers = test_layers;
    keyboard.landscape_layers = test_layers;
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
    wl_pointer_button(NULL, NULL, 0, 8, 0, WL_POINTER_BUTTON_STATE_RELEASED);
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
    wl_pointer_button(NULL, NULL, 0, 2, 0, WL_POINTER_BUTTON_STATE_PRESSED);
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
test_cancelled_feedback_once(void)
{
    struct key key = {.type = Code, .code = KEY_A};

    reset();
    popup_xdg_surface_configured = true;
    assert(mod_swipe_begin(&mod_swipe, 1, 0, 0, 1, &key, 60, true, 0));
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(24), 0);
    assert(mod_swipe.action == ModSwipeCancelled);
    assert(feedback_clears == 1);
    wl_touch_motion(NULL, NULL, 3, 1, wl_fixed_from_int(48), 0);
    assert(feedback_clears == 1);
}

static void
test_orientation_output_and_configure(void)
{
    struct Output output = {.w = 100, .h = 200, .scale = 1};
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
    wl_surface_enter(NULL, NULL, wl_outputs[0].data);
    expect_cancelled(0);
    assert(current_output == &wl_outputs[0]);

    reset();
    seed_deferred_glide();
    layer_surface = (struct zwlr_layer_surface_v1 *)&sentinel;
    compositor = (struct wl_compositor *)&sentinel;
    wm_base = (struct xdg_wm_base *)&sentinel;
    popup_xdg_positioner = (struct xdg_positioner *)&sentinel;
    empty_region = (struct wl_region *)&sentinel;
    draw_surf.surf = (struct wl_surface *)&sentinel;
    layer_surface_configure(NULL, layer_surface, 1, 320, 240);
    expect_cancelled(1);
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
    wl_pointer_button(NULL, NULL, 0, 1, 0, WL_POINTER_BUTTON_STATE_PRESSED);
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
    assert(!mod_swipe.active);
    assert(layout_draws == 1);
    assert(popup_clears == 1);
    assert(surface_flips == 2 + extra_flips);
    assert(key_releases == 0);
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
    seed_deferred_glide();
    pointer = (struct wl_pointer *)&pointer_sentinel;
    touch = (struct wl_touch *)&touch_sentinel;
    seat_handle_capabilities(NULL, NULL, 0);
    expect_cancelled(0);
    assert(pointer == NULL && touch == NULL);

    reset();
    seed_deferred_glide();
    layer_surface = (struct zwlr_layer_surface_v1 *)&pointer_sentinel;
    draw_surf.surf = (struct wl_surface *)&pointer_sentinel;
    hide();
    expect_cancelled(0);
    assert(hidden && layer_surface == NULL);

    reset();
    seed_deferred_glide();
    layer_surface = (struct zwlr_layer_surface_v1 *)&pointer_sentinel;
    draw_surf.surf = (struct wl_surface *)&pointer_sentinel;
    layer_surface_closed(NULL, layer_surface);
    expect_cancelled(0);
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
    assert(surface_flips ==
           (action == ModSwipeGlide && invalid ? 3U : 2U));
    assert(emitted_words == (action == ModSwipeGlide && !invalid));
    assert(popup_feedbacks ==
           (action == ModSwipeGlide && invalid ? 1U : 0U));
    assert(activated_keys == 0);
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
    wl_touch_motion(NULL, NULL, 2, 1, wl_fixed_from_int(24), 0);
    assert(mod_swipe.action == ModSwipeGlideCandidate);
    wl_touch_motion(NULL, NULL, 3, 1, wl_fixed_from_int(48), 0);
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

int
main(void)
{
    expect_final_redraw(false, ModSwipeGlide, "helo");
    expect_final_redraw(true, ModSwipeGlide, "helo");
    expect_final_redraw(false, ModSwipeGlideCandidate, "helo");
    expect_final_redraw(false, ModSwipeCancelled, "helo");
    test_invalid_release_actions();
    test_glide_no_match_feedback();
    test_glide_draw_order();
    test_non_code_key_cannot_extend_glide_trace();
    test_cancel_active_input();
    test_lifecycle_boundaries();
    test_orientation_output_and_configure();
    test_compose_dismissal();
    test_touch_cancels_held_pointer_before_compose_transition();
    test_pointer_down_is_ignored_while_touch_owns_input();
    test_touch_transition_cancels_before_activation();
    test_cancelled_feedback_once();
    puts("main glide release tests passed");
    return 0;
}
