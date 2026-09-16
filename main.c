#include "proto/virtual-keyboard-unstable-v1-client-protocol.h"
#include "proto/wlr-layer-shell-unstable-v1-client-protocol.h"
#include "proto/xdg-shell-client-protocol.h"
#include "proto/fractional-scale-v1-client-protocol.h"
#include "proto/viewporter-client-protocol.h"
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wchar.h>

#include "keyboard.h"
#include "glide.h"
#include "glide-learning.h"
#include "letters.h"
#include "mod-swipe.h"
#include "config.h"

/* lazy die macro */
#define die(...)                                                               \
    fprintf(stderr, __VA_ARGS__);                                              \
    exit(1)

/* array size */
#define countof(x) (sizeof(x) / sizeof(*x))

/* client state */
static const char *namespace = "wlroots";
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wl_touch *touch;
static struct wl_region *empty_region;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zwlr_layer_surface_v1 *layer_surface;
static struct xdg_wm_base *wm_base;
static struct xdg_surface *popup_xdg_surface;
static struct xdg_popup *popup_xdg_popup;
static struct xdg_positioner *popup_xdg_positioner;
static struct zwp_virtual_keyboard_manager_v1 *vkbd_mgr;
static struct wp_fractional_scale_v1 *wfs_draw_surf;
static struct wp_fractional_scale_manager_v1 *wfs_mgr;
static struct wp_viewport *draw_surf_viewport, *popup_draw_surf_viewport;
static struct wp_viewporter *viewporter;
static bool popup_xdg_surface_configured;

struct Output {
    uint32_t name;
    uint32_t w, h;
    double scale;
    struct wl_output *data;
};
static struct Output *current_output;

#define WL_OUTPUTS_LIMIT 8
static struct Output wl_outputs[WL_OUTPUTS_LIMIT];
static int wl_outputs_size;

/* drawing */
static struct drw draw_ctx;
static struct drwsurf draw_surf, popup_draw_surf, visibility_draw_surf;

/* layer surface parameters */
static uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
static uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

#define VISIBILITY_CONTROL_WIDTH 92
#define VISIBILITY_CONTROL_HEIGHT 30
#define VISIBILITY_MAX_SCALE 8

enum visibility_state {
    VisibilityFullyHidden = 0,
    VisibilityExpanded,
    VisibilityCollapsed,
};

enum visibility_input_owner {
    VisibilityInputNone = 0,
    VisibilityInputTouch,
    VisibilityInputPointer,
};

struct visibility_input {
    enum visibility_input_owner owner;
    uint32_t token;
    bool valid;
};

/* application state */
static bool run_display = true;
static int cur_x = -1, cur_y = -1;
static int32_t cur_touch_id = -1;
static bool cur_press = false;
static uint32_t cur_button;
static struct kbd keyboard;
static uint32_t height, normal_height, landscape_height;
static int rounding = DEFAULT_ROUNDING;
static enum visibility_state visibility = VisibilityExpanded;
static bool keyboard_needs_configure;
static struct zwlr_layer_surface_v1 *visibility_layer_surface;
static struct wp_viewport *visibility_draw_surf_viewport;
static struct wp_fractional_scale_v1 *wfs_visibility_draw_surf;
static bool visibility_configured;
static double visibility_preferred_fractional_scale;
static bool pointer_on_visibility;
static bool pointer_inside_visibility;
static struct visibility_input visibility_input;
static bool mod_swipe_enabled = false;
static struct mod_swipe_state mod_swipe;
static struct glide_learning_sink glide_learning = {.fd = -1};
static uint32_t last_input_time;

/* event handler prototypes */
static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                             uint32_t serial, struct wl_surface *surface,
                             wl_fixed_t surface_x, wl_fixed_t surface_y);
static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                             uint32_t serial, struct wl_surface *surface);
static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                              uint32_t time, wl_fixed_t surface_x,
                              wl_fixed_t surface_y);
static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, uint32_t time, uint32_t button,
                              uint32_t state);
static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                            uint32_t time, uint32_t axis, wl_fixed_t value);

static void wl_touch_down(void *data, struct wl_touch *wl_touch,
                          uint32_t serial, uint32_t time,
                          struct wl_surface *surface, int32_t id, wl_fixed_t x,
                          wl_fixed_t y);
static void wl_touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
                        uint32_t time, int32_t id);
static void wl_touch_motion(void *data, struct wl_touch *wl_touch,
                            uint32_t time, int32_t id, wl_fixed_t x,
                            wl_fixed_t y);
static void wl_touch_frame(void *data, struct wl_touch *wl_touch);
static void wl_touch_cancel(void *data, struct wl_touch *wl_touch);
static void wl_touch_shape(void *data, struct wl_touch *wl_touch, int32_t id,
                           wl_fixed_t major, wl_fixed_t minor);
static void wl_touch_orientation(void *data, struct wl_touch *wl_touch,
                                 int32_t id, wl_fixed_t orientation);

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                                     enum wl_seat_capability caps);
static void seat_handle_name(void *data, struct wl_seat *wl_seat,
                             const char *name);

static void wl_surface_enter(void *data, struct wl_surface *wl_surface,
                             struct wl_output *wl_output);
static void wl_surface_leave(void *data, struct wl_surface *wl_surface,
                             struct wl_output *wl_output);

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version);
static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name);

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h);
static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface);
static void visibility_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t w, uint32_t h);
static void visibility_surface_closed(void *data,
                                      struct zwlr_layer_surface_v1 *surface);
static void flip_landscape();
static void show();
static void collapse();
static void cancel_active_input(uint32_t time);
static void reset_input_lifecycle(uint32_t time);
static void destroy_popup_surface();
static void destroy_keyboard_surfaces(uint32_t time);
static void hide_visibility_control();
static void show_visibility_control();
static void draw_visibility_control(bool pressed);
static void resize_visibility_control();
static void refresh_visibility_control_scale();
static void cancel_visibility_input();
static double visibility_scale();

static void
draw_glide_trace(void)
{
    kbd_draw_layout(&keyboard);
    kbd_clear_last_popup(&keyboard);
    for (size_t i = 0; i < mod_swipe.trace_length; i++) {
        kbd_draw_key(&keyboard, mod_swipe.trace_keys[i], Swipe);
    }
    drwsurf_flip(keyboard.surf);
    drwsurf_flip(keyboard.popup_surf);
}

static void
finish_deferred_gesture(void)
{
    kbd_draw_layout(&keyboard);
    kbd_clear_last_popup(&keyboard);
    drwsurf_flip(keyboard.surf);
    drwsurf_flip(keyboard.popup_surf);
}

/* event handlers */
static const struct wl_pointer_listener pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
};

static const struct wl_touch_listener touch_listener = {
    .down = wl_touch_down,
    .up = wl_touch_up,
    .motion = wl_touch_motion,
    .frame = wl_touch_frame,
    .cancel = wl_touch_cancel,
    .shape = wl_touch_shape,
    .orientation = wl_touch_orientation,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static const struct wl_surface_listener surface_listener = {
    .enter = wl_surface_enter,
    .leave = wl_surface_leave,
};

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static const struct zwlr_layer_surface_v1_listener
    visibility_surface_listener = {
        .configure = visibility_surface_configure,
        .closed = visibility_surface_closed,
};

static bool
visibility_is_control_surface(struct wl_surface *surface)
{
    return visibility_draw_surf.surf &&
           surface == visibility_draw_surf.surf;
}

static bool
visibility_fixed_point_inside(wl_fixed_t x, wl_fixed_t y)
{
    return x >= 0 && y >= 0 &&
           x < wl_fixed_from_int(VISIBILITY_CONTROL_WIDTH) &&
           y < wl_fixed_from_int(VISIBILITY_CONTROL_HEIGHT);
}

static bool
visibility_input_begin(enum visibility_input_owner owner, uint32_t token)
{
    if (visibility_input.owner != VisibilityInputNone) {
        return false;
    }
    visibility_input = (struct visibility_input){.owner = owner,
                                                 .token = token,
                                                 .valid = true};
    return true;
}

static void
visibility_input_start(enum visibility_input_owner owner, uint32_t token,
                       uint32_t time)
{
    if (!visibility_input_begin(owner, token)) {
        return;
    }
    glide_learning_clear(&glide_learning);
    kbd_clear_glide_undo(&keyboard);
    cancel_active_input(time);
    draw_visibility_control(true);
}

static bool
visibility_input_motion(enum visibility_input_owner owner, uint32_t token,
                        bool inside)
{
    if (visibility_input.owner != owner || visibility_input.token != token ||
        !visibility_input.valid || inside) {
        return false;
    }
    visibility_input.valid = false;
    return true;
}

static bool
visibility_input_finish(enum visibility_input_owner owner, uint32_t token,
                        bool inside)
{
    bool activate;

    if (visibility_input.owner != owner || visibility_input.token != token) {
        return false;
    }
    activate = visibility_input.valid && inside;
    visibility_input = (struct visibility_input){0};
    return activate;
}

static bool
normal_input_active(void)
{
    return mod_swipe.active || cur_press || cur_button || cur_touch_id >= 0 ||
           keyboard.last_press ||
           keyboard.candidates.owner != KbdCandidateOwnerNone;
}

static void
cancel_visibility_input(void)
{
    if (visibility_input.owner == VisibilityInputNone) {
        return;
    }
    visibility_input = (struct visibility_input){0};
    draw_visibility_control(false);
}

static void
activate_visibility_control(void)
{
    if (visibility == VisibilityCollapsed) {
        show();
    } else if (visibility == VisibilityExpanded) {
        collapse();
    }
}

/* configuration, allows nested code to access above variables */

char *
estrdup(const char *s)
{
    char *p;

    if (!(p = strdup(s))) {
        fprintf(stderr, "strdup:");
        exit(6);
    }

    return p;
}

void
wl_touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
              uint32_t time, struct wl_surface *surface, int32_t id,
              wl_fixed_t x, wl_fixed_t y)
{
    int32_t touch_x = wl_fixed_to_int(x);
    int32_t touch_y = wl_fixed_to_int(y);

    last_input_time = time;
    if (visibility_is_control_surface(surface)) {
        if (!normal_input_active() && visibility_fixed_point_inside(x, y)) {
            visibility_input_start(VisibilityInputTouch, (uint32_t)id, time);
        }
        return;
    }
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    if (!popup_xdg_surface_configured) {
        return;
    }

    struct key *next_key;

    if (kbd_candidate_touch_down(&keyboard, id, touch_x, touch_y) !=
        KbdCandidateMiss) {
        return;
    }
    if (mod_swipe_enabled && mod_swipe.active) {
        return;
    }

    cancel_active_input(time);
    next_key = touch_x >= 0 && touch_y >= 0
                   ? kbd_get_key(&keyboard, touch_x, touch_y)
                   : NULL;
    if (next_key) {
        if (kbd_key_changes_interpretation(&keyboard, next_key)) {
            kbd_activate_key(&keyboard, next_key, time, NoMod);
            return;
        }
        if (mod_swipe_enabled) {
            char start_letter = 0;
            bool deferred = keyboard.compose == 0 && next_key->type == Code &&
                            mod_swipe_keycode_is_character(next_key->code);
            if (deferred) {
                kbd_glide_letter(&keyboard, next_key, &start_letter);
            }
            mod_swipe_begin(&mod_swipe, id, touch_x, touch_y, time, next_key,
                            next_key->h, deferred, start_letter);
            if (deferred) {
                kbd_show_key_feedback(&keyboard, next_key, NULL);
            } else {
                kbd_press_key(&keyboard, next_key, time);
            }
        } else {
            cur_touch_id = id;
            kbd_press_key(&keyboard, next_key, time);
        }
    } else {
        if (keyboard.compose) {
            keyboard.compose = 0;
            kbd_switch_layout(&keyboard, keyboard.prevlayout,
                              keyboard.last_abc_index);
        }
    }
}

void
wl_touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
            uint32_t time, int32_t id)
{
    struct mod_swipe_result result;

    last_input_time = time;
    if (visibility_input.owner == VisibilityInputTouch) {
        bool activate;

        if (visibility_input.token != (uint32_t)id) {
            return;
        }
        activate = visibility_input_finish(VisibilityInputTouch, (uint32_t)id,
                                           true);
        if (activate) {
            activate_visibility_control();
        } else {
            draw_visibility_control(false);
        }
        return;
    }
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    if (kbd_candidate_touch_up(&keyboard, id, time) != KbdCandidateMiss) {
        return;
    }
    if (mod_swipe_enabled) {
        if (!mod_swipe_finish(&mod_swipe, id, time, &result)) {
            return;
        }
        if (result.deferred &&
            (result.invalid ||
             (result.action != ModSwipePending &&
              result.action != ModSwipeControlCandidate &&
              result.action != ModSwipeAltCandidate &&
              result.action != ModSwipeControlAltCandidate))) {
            kbd_clear_glide_undo(&keyboard);
        }
        if (!result.deferred) {
            kbd_release_key(&keyboard, result.time);
        } else if ((result.invalid && result.action != ModSwipeGlide) ||
                   result.action == ModSwipeGlideCandidate ||
                   result.action == ModSwipeCancelled) {
            finish_deferred_gesture();
        } else if (result.action == ModSwipePending) {
            kbd_activate_key(&keyboard, result.key, result.time, NoMod);
        } else if (result.action == ModSwipeControlCandidate) {
            kbd_activate_key(&keyboard, result.key, result.time, Ctrl);
        } else if (result.action == ModSwipeAltCandidate) {
            kbd_activate_key(&keyboard, result.key, result.time, Alt);
        } else if (result.action == ModSwipeControlAltCandidate) {
            kbd_activate_key(&keyboard, result.key, result.time, Ctrl | Alt);
        } else if (result.action == ModSwipeGlide) {
            struct glide_result matches;
            struct glide_geometry geometry;
            bool emitted = false;
            bool observed;

            if (!result.invalid && result.endpoint_mapped &&
                kbd_glide_geometry(&keyboard, &geometry)) {
                glide_recognize(result.trace, result.trace_points,
                                result.trace_length, &geometry, &matches);
                if (glide_learning.correction_pending) {
                    glide_learning_resolve_correction(&glide_learning, true);
                } else if (glide_learning.pending_has_candidates) {
                    glide_learning_resolve(&glide_learning,
                                           GLIDE_LEARNING_TOP_COMMITTED, 0);
                }
                emitted =
                    kbd_commit_glide_result(&keyboard, &matches, result.time);
                observed = glide_learning_observe(
                    &glide_learning, result.trace, result.trace_points,
                    result.trace_length, &geometry, &matches);
                if (!emitted && observed) {
                    kbd_show_learning_choices(&keyboard);
                    emitted = true;
                }
            }
            finish_deferred_gesture();
            if (!emitted) {
                kbd_show_popup_feedback(&keyboard, result.key, "?");
                drwsurf_flip(keyboard.popup_surf);
            }
        }
        return;
    }
    if (!popup_xdg_surface_configured) {
        return;
    }
    if (cur_touch_id != id) {
        return;
    }
    cur_touch_id = -1;
    kbd_release_key(&keyboard, time);
}

void
wl_touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time,
                int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    int32_t touch_x = wl_fixed_to_int(x);
    int32_t touch_y = wl_fixed_to_int(y);

    last_input_time = time;
    if (visibility_input.owner == VisibilityInputTouch) {
        if (visibility_input.token != (uint32_t)id) {
            return;
        }
        if (visibility_input_motion(
                VisibilityInputTouch, (uint32_t)id,
                visibility_fixed_point_inside(x, y))) {
            draw_visibility_control(false);
        }
        return;
    }
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    if (!popup_xdg_surface_configured) {
        return;
    }

    if (kbd_candidate_touch_motion(&keyboard, id, touch_x, touch_y) !=
        KbdCandidateMiss) {
        return;
    }
    if (mod_swipe_enabled) {
        enum mod_swipe_action previous;
        struct key *intersection;
        char letter = 0;
        if (!mod_swipe_owns(&mod_swipe, id)) {
            return;
        }
        if (!mod_swipe.deferred) {
            mod_swipe_update(&mod_swipe, id, touch_x, touch_y, time, NULL, 0);
            kbd_motion_key(&keyboard, time, (uint32_t)touch_x,
                           (uint32_t)touch_y);
        } else {
            previous = mod_swipe.action;
            intersection = touch_x >= 0 && touch_y >= 0
                               ? kbd_get_key(&keyboard, touch_x, touch_y)
                               : NULL;
            if (intersection && intersection->type == Code) {
                glide_letter_from_evdev(intersection->code, &letter);
            }
            mod_swipe_update(&mod_swipe, id, touch_x, touch_y, time,
                             intersection, letter);
            if (mod_swipe.entered_glide) {
                draw_glide_trace();
            } else if (mod_swipe.action == ModSwipeGlide &&
                       mod_swipe.trace_changed) {
                kbd_draw_key(&keyboard,
                             mod_swipe.trace_keys[mod_swipe.trace_length - 1],
                             Swipe);
                drwsurf_flip(keyboard.surf);
            } else if (previous == ModSwipePending &&
                       mod_swipe.action == ModSwipeGlideCandidate) {
                kbd_clear_key_feedback(&keyboard, mod_swipe.key);
            } else if (previous != ModSwipeControlCandidate &&
                       mod_swipe.action == ModSwipeControlCandidate) {
                kbd_show_key_feedback(&keyboard, mod_swipe.key, "C-");
            } else if (previous != ModSwipeAltCandidate &&
                       mod_swipe.action == ModSwipeAltCandidate) {
                kbd_show_key_feedback(&keyboard, mod_swipe.key, "M-");
            } else if (previous != ModSwipeControlAltCandidate &&
                       mod_swipe.action == ModSwipeControlAltCandidate) {
                kbd_show_key_feedback(&keyboard, mod_swipe.key, "C-M-");
            } else if (previous != ModSwipeCancelled &&
                       mod_swipe.action == ModSwipeCancelled) {
                kbd_clear_key_feedback(&keyboard, mod_swipe.key);
            }
        }
        return;
    }

    kbd_motion_key(&keyboard, time, touch_x, touch_y);
}

void
wl_touch_frame(void *data, struct wl_touch *wl_touch)
{
}

void
wl_touch_cancel(void *data, struct wl_touch *wl_touch)
{
    if (visibility_input.owner == VisibilityInputTouch) {
        cancel_visibility_input();
    }
    reset_input_lifecycle(last_input_time);
}

void
wl_touch_shape(void *data, struct wl_touch *wl_touch, int32_t id,
               wl_fixed_t major, wl_fixed_t minor)
{
}

void
wl_touch_orientation(void *data, struct wl_touch *wl_touch, int32_t id,
                     wl_fixed_t orientation)
{
}

void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                 struct wl_surface *surface, wl_fixed_t surface_x,
                 wl_fixed_t surface_y)
{
    cur_x = wl_fixed_to_int(surface_x);
    cur_y = wl_fixed_to_int(surface_y);
    pointer_on_visibility = visibility_is_control_surface(surface);
    pointer_inside_visibility =
        pointer_on_visibility &&
        visibility_fixed_point_inside(surface_x, surface_y);
    if (pointer_on_visibility ||
        visibility_input.owner != VisibilityInputNone) {
        return;
    }
    kbd_candidate_pointer_motion(&keyboard, cur_x, cur_y);
}

void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                 struct wl_surface *surface)
{
    if (visibility_is_control_surface(surface)) {
        pointer_on_visibility = false;
        pointer_inside_visibility = false;
        if (visibility_input_motion(VisibilityInputPointer,
                                    visibility_input.token, false)) {
            draw_visibility_control(false);
        }
        cur_x = cur_y = -1;
        return;
    }
    cur_x = cur_y = -1;
    pointer_inside_visibility = false;
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    kbd_candidate_pointer_motion(&keyboard, cur_x, cur_y);
}

void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                  wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    last_input_time = time;
    cur_x = wl_fixed_to_int(surface_x);
    cur_y = wl_fixed_to_int(surface_y);
    pointer_inside_visibility =
        pointer_on_visibility &&
        visibility_fixed_point_inside(surface_x, surface_y);
    if (visibility_input.owner == VisibilityInputPointer) {
        if (visibility_input_motion(
                VisibilityInputPointer, visibility_input.token,
                pointer_inside_visibility)) {
            draw_visibility_control(false);
        }
        return;
    }
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    if (pointer_on_visibility || !popup_xdg_surface_configured) {
        return;
    }

    if (kbd_candidate_pointer_motion(&keyboard, cur_x, cur_y) !=
        KbdCandidateMiss) {
        return;
    }
    if (mod_swipe.active) {
        return;
    }
    if (cur_press) {
        kbd_motion_key(&keyboard, time, cur_x, cur_y);
    }
}

void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                  uint32_t time, uint32_t button, uint32_t state)
{
    bool pressed;
    int32_t pointer_x = cur_x;
    int32_t pointer_y = cur_y;
    struct key *next_key;
    enum kbd_candidate_event candidate_event;

    if (state != WL_POINTER_BUTTON_STATE_PRESSED &&
        state != WL_POINTER_BUTTON_STATE_RELEASED) {
        return;
    }
    pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
    last_input_time = time;
    if (visibility_input.owner == VisibilityInputPointer) {
        bool activate;

        if (pressed || visibility_input.token != button) {
            return;
        }
        activate = visibility_input_finish(
            VisibilityInputPointer, button, pointer_inside_visibility);
        if (activate) {
            activate_visibility_control();
        } else {
            draw_visibility_control(false);
        }
        return;
    }
    if (visibility_input.owner != VisibilityInputNone) {
        return;
    }
    if (!popup_xdg_surface_configured) {
        if (!pointer_on_visibility) {
            return;
        }
    }
    if (!pointer_on_visibility ||
        keyboard.candidates.owner != KbdCandidateOwnerNone) {
        candidate_event = kbd_candidate_pointer_button(
            &keyboard, button, pressed,
            pointer_on_visibility ? -1 : pointer_x,
            pointer_on_visibility ? -1 : pointer_y, time);
        if (candidate_event != KbdCandidateMiss) {
            if (pressed && candidate_event == KbdCandidateDismissed &&
                !cur_button) {
                cur_button = button;
            }
            return;
        }
    }
    if (mod_swipe.active) {
        return;
    }

    if (pressed && cur_button) {
        return;
    }
    if (!pressed && button != cur_button) {
        return;
    }
    if (!pressed) {
        cur_button = 0;
        if (!cur_press) {
            return;
        }
        cur_press = false;
        kbd_release_key(&keyboard, time);
        return;
    }

    if (pointer_on_visibility) {
        if (!normal_input_active() && pointer_inside_visibility) {
            visibility_input_start(VisibilityInputPointer, button, time);
        }
        return;
    }

    cancel_active_input(time);
    next_key = pointer_x >= 0 && pointer_y >= 0
                   ? kbd_get_key(&keyboard, pointer_x, pointer_y)
                   : NULL;
    if (next_key && kbd_key_changes_interpretation(&keyboard, next_key)) {
        kbd_activate_key(&keyboard, next_key, time, NoMod);
        return;
    }
    if (!next_key && keyboard.compose) {
        keyboard.compose = 0;
        kbd_switch_layout(&keyboard, keyboard.prevlayout,
                          keyboard.last_abc_index);
        return;
    }
    cur_press = true;
    cur_button = button;
    cur_x = pointer_x;
    cur_y = pointer_y;
    if (next_key) {
        kbd_press_key(&keyboard, next_key, time);
    }
}

void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                uint32_t axis, wl_fixed_t value)
{
    if (visibility_input.owner == VisibilityInputPointer) {
        cancel_visibility_input();
        return;
    }
    if (visibility_input.owner != VisibilityInputNone ||
        pointer_on_visibility) {
        return;
    }
    if (!popup_xdg_surface_configured) {
        return;
    }

    last_input_time = time;
    glide_learning_clear(&glide_learning);
    kbd_clear_glide_undo(&keyboard);
    cancel_active_input(time);
    kbd_next_layer(&keyboard, NULL, (value >= 0));
    drwsurf_flip(keyboard.surf);
}

void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
        if (pointer == NULL) {
            pointer = wl_seat_get_pointer(wl_seat);
            wl_pointer_add_listener(pointer, &pointer_listener, NULL);
        }
    } else {
        if (pointer != NULL) {
            wl_pointer_destroy(pointer);
            pointer = NULL;
        }
    }
    if ((caps & WL_SEAT_CAPABILITY_TOUCH)) {
        if (touch == NULL) {
            touch = wl_seat_get_touch(wl_seat);
            wl_touch_add_listener(touch, &touch_listener, NULL);
        }
    } else {
        if (touch != NULL) {
            wl_touch_destroy(touch);
            touch = NULL;
        }
    }
}

void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
}

void
wl_surface_enter(void *data, struct wl_surface *wl_surface,
                 struct wl_output *wl_output)
{
    struct Output *new_output = current_output;
    bool control_surface = visibility_is_control_surface(wl_surface);

    if (control_surface && layer_surface && visibility == VisibilityExpanded) {
        return;
    }
    for (int i = 0; i < wl_outputs_size; i += 1) {
        if (wl_outputs[i].data == wl_output) {
            new_output = &wl_outputs[i];
            break;
        }
    }
    if (new_output == current_output) {
        return;
    }

    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    current_output = new_output;
    keyboard.preferred_scale = current_output->scale;
    hide_visibility_control();
    if (!control_surface && layer_surface) {
        destroy_keyboard_surfaces(last_input_time);
        show();
        return;
    }
    flip_landscape();
    show_visibility_control();
}

void
wl_surface_leave(void *data, struct wl_surface *wl_surface,
                 struct wl_output *wl_output)
{
}

static void
display_handle_geometry(void *data, struct wl_output *wl_output, int x, int y,
                        int physical_width, int physical_height, int subpixel,
                        const char *make, const char *model, int transform)
{
    struct Output *output = data;

    // Swap width and height on rotated displays
    if (transform % 2 != 0) {
        int tmp = physical_width;
        physical_width = physical_height;
        physical_height = tmp;
    }

    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    output->w = physical_width;
    output->h = physical_height;

    if (current_output == output) {
        flip_landscape();
    };
}

static void
display_handle_done(void *data, struct wl_output *wl_output)
{
}

static void
display_handle_scale(void *data, struct wl_output *wl_output, int32_t scale)
{
    struct Output *output = data;

    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    output->scale = scale;

    if (current_output == output) {
        keyboard.preferred_scale = scale;
        flip_landscape();
        if (!wfs_visibility_draw_surf) {
            refresh_visibility_control_scale();
        }
    };
}

static void
display_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                    int width, int height, int refresh)
{
}

static const struct wl_output_listener output_listener = {
    .geometry = display_handle_geometry,
    .mode = display_handle_mode,
    .done = display_handle_done,
    .scale = display_handle_scale};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

void
handle_global(void *data, struct wl_registry *registry, uint32_t name,
              const char *interface, uint32_t version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, 3);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        draw_ctx.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (wl_outputs_size < WL_OUTPUTS_LIMIT) {
            struct Output *output = &wl_outputs[wl_outputs_size];
            output->data =
                wl_registry_bind(registry, name, &wl_output_interface, 2);
            output->name = name;
            output->scale = 1;
            wl_output_add_listener(output->data, &output_listener, output);
            wl_outputs_size += 1;
        }
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        layer_shell =
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface,
                      wp_fractional_scale_manager_v1_interface.name) == 0) {
        wfs_mgr = wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        viewporter =
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface,
                      zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        vkbd_mgr = wl_registry_bind(
            registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    for (int i = 0; i < wl_outputs_size; i += 1) {
        if (wl_outputs[i].name == name) {
            int removed_index = i;
            int current_index =
                current_output ? (int)(current_output - wl_outputs) : -1;

            cancel_visibility_input();
            reset_input_lifecycle(last_input_time);
            wl_output_destroy(wl_outputs[i].data);
            for (; i < wl_outputs_size - 1; i += 1) {
                wl_outputs[i] = wl_outputs[i + 1];
            }
            wl_outputs_size -= 1;
            if (current_index == removed_index) {
                current_output = NULL;
            } else if (current_index > removed_index) {
                current_output = &wl_outputs[current_index - 1];
            }
            break;
        }
    }
}

static void
xdg_popup_surface_configure(void *data, struct xdg_surface *xdg_surface,
                            uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
    popup_xdg_surface_configured = true;
    drwsurf_flip(&popup_draw_surf);
}

static const struct xdg_surface_listener xdg_popup_surface_listener = {
    .configure = xdg_popup_surface_configure,
};

static void
xdg_popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x,
                    int32_t y, int32_t width, int32_t height)
{
}

static void
xdg_popup_done(void *data, struct xdg_popup *xdg_popup)
{
}

static const struct xdg_popup_listener xdg_popup_listener = {
    .configure = xdg_popup_configure,
    .popup_done = xdg_popup_done,
};

static void
wp_fractional_scale_preferred_scale(
    void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1,
    uint32_t scale)
{
    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    if (!scale || scale > VISIBILITY_MAX_SCALE * 120) {
        return;
    }
    keyboard.preferred_fractional_scale = (double)scale / 120;
    if (!layer_surface || keyboard_needs_configure || !draw_surf.buf ||
        !popup_draw_surf.buf) {
        return;
    }
    keyboard.scale = keyboard.preferred_fractional_scale;
    kbd_resize(&keyboard, layouts, NumLayouts);
    drwsurf_flip(&draw_surf);
    if (popup_xdg_surface_configured) {
        drwsurf_flip(&popup_draw_surf);
    }
}

static const struct wp_fractional_scale_v1_listener
    wp_fractional_scale_listener = {
        .preferred_scale = wp_fractional_scale_preferred_scale,
};

static double
visibility_scale(void)
{
    double scale;

    if (visibility_preferred_fractional_scale) {
        scale = visibility_preferred_fractional_scale;
    } else {
        scale = keyboard.preferred_scale;
    }
    return scale > 0 && scale <= VISIBILITY_MAX_SCALE ? scale : 1;
}

static struct wl_output *
selected_output(void)
{
    return current_output ? current_output->data : NULL;
}

static void
resize_visibility_control(void)
{
    double scale = visibility_scale();

    if (visibility_draw_surf_viewport) {
        wp_viewport_set_destination(visibility_draw_surf_viewport,
                                    VISIBILITY_CONTROL_WIDTH,
                                    VISIBILITY_CONTROL_HEIGHT);
    } else {
        wl_surface_set_buffer_scale(visibility_draw_surf.surf, scale);
    }
    drwsurf_resize(&visibility_draw_surf, VISIBILITY_CONTROL_WIDTH,
                   VISIBILITY_CONTROL_HEIGHT, scale);
}

static void
refresh_visibility_control_scale(void)
{
    if (!visibility_draw_surf.surf || !visibility_configured) {
        return;
    }
    resize_visibility_control();
    draw_visibility_control(false);
}

static void
visibility_fractional_scale_preferred_scale(
    void *data, struct wp_fractional_scale_v1 *fractional_scale,
    uint32_t scale)
{
    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    if (!scale || scale > VISIBILITY_MAX_SCALE * 120) {
        return;
    }
    visibility_preferred_fractional_scale = (double)scale / 120;
    refresh_visibility_control_scale();
}

static const struct wp_fractional_scale_v1_listener
    visibility_fractional_scale_listener = {
        .preferred_scale = visibility_fractional_scale_preferred_scale,
};

static void
destroy_popup_surface(void)
{
    if (popup_xdg_popup) {
        xdg_popup_destroy(popup_xdg_popup);
        popup_xdg_popup = NULL;
    }
    if (popup_xdg_surface) {
        xdg_surface_destroy(popup_xdg_surface);
        popup_xdg_surface = NULL;
    }
    if (popup_draw_surf_viewport) {
        wp_viewport_destroy(popup_draw_surf_viewport);
        popup_draw_surf_viewport = NULL;
    }
    if (popup_draw_surf.surf) {
        drwsurf_reset(&popup_draw_surf);
        wl_surface_destroy(popup_draw_surf.surf);
        popup_draw_surf.surf = NULL;
    }
    popup_xdg_surface_configured = false;
}

static void
destroy_keyboard_surfaces(uint32_t time)
{
    cancel_visibility_input();
    reset_input_lifecycle(time);
    keyboard.preferred_fractional_scale = 0;
    if (!layer_surface) {
        return;
    }

    destroy_popup_surface();

    if (wfs_draw_surf) {
        wp_fractional_scale_v1_destroy(wfs_draw_surf);
        wfs_draw_surf = NULL;
    }
    if (draw_surf_viewport) {
        wp_viewport_destroy(draw_surf_viewport);
        draw_surf_viewport = NULL;
    }
    zwlr_layer_surface_v1_destroy(layer_surface);
    layer_surface = NULL;
    drwsurf_reset(&draw_surf);
    wl_surface_destroy(draw_surf.surf);
    draw_surf.surf = NULL;
    keyboard_needs_configure = false;
}

static void
draw_visibility_control(bool pressed)
{
    struct clr_scheme *scheme;
    const char *label;

    if (!visibility_draw_surf.surf || !visibility_draw_surf.buf ||
        !visibility_configured) {
        return;
    }
    scheme = &keyboard.schemes[1];
    label = visibility == VisibilityCollapsed ? "Keyboard" : "Hide";
    drw_fill_rectangle(&visibility_draw_surf, keyboard.schemes[0].bg, 0, 0,
                       VISIBILITY_CONTROL_WIDTH, VISIBILITY_CONTROL_HEIGHT, 0);
    draw_inset(&visibility_draw_surf, 0, 0, VISIBILITY_CONTROL_WIDTH,
               VISIBILITY_CONTROL_HEIGHT, 2,
               pressed ? scheme->high : scheme->fg, scheme->rounding);
    drw_draw_text(&visibility_draw_surf, scheme->text, 0, 0,
                  VISIBILITY_CONTROL_WIDTH, VISIBILITY_CONTROL_HEIGHT, 2,
                  label, scheme->font_description);
    wl_surface_damage(visibility_draw_surf.surf, 0, 0,
                      VISIBILITY_CONTROL_WIDTH, VISIBILITY_CONTROL_HEIGHT);
    drwsurf_flip(&visibility_draw_surf);
}

static void
show_visibility_control(void)
{
    if (visibility_layer_surface) {
        draw_visibility_control(false);
        return;
    }
    visibility_draw_surf.surf = wl_compositor_create_surface(compositor);
    wl_surface_add_listener(visibility_draw_surf.surf, &surface_listener, NULL);
    if (wfs_mgr && viewporter) {
        wfs_visibility_draw_surf =
            wp_fractional_scale_manager_v1_get_fractional_scale(
                wfs_mgr, visibility_draw_surf.surf);
        wp_fractional_scale_v1_add_listener(
            wfs_visibility_draw_surf, &visibility_fractional_scale_listener,
            NULL);
        visibility_draw_surf_viewport =
            wp_viewporter_get_viewport(viewporter, visibility_draw_surf.surf);
    }
    visibility_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, visibility_draw_surf.surf, selected_output(), layer,
        namespace);
    zwlr_layer_surface_v1_set_size(visibility_layer_surface,
                                   VISIBILITY_CONTROL_WIDTH,
                                   VISIBILITY_CONTROL_HEIGHT);
    zwlr_layer_surface_v1_set_anchor(
        visibility_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                      ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(visibility_layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(visibility_layer_surface,
                                                     false);
    zwlr_layer_surface_v1_add_listener(
        visibility_layer_surface, &visibility_surface_listener, NULL);
    wl_surface_commit(visibility_draw_surf.surf);
}

static void
hide_visibility_control(void)
{
    visibility_input = (struct visibility_input){0};
    pointer_on_visibility = false;
    pointer_inside_visibility = false;
    visibility_configured = false;
    visibility_preferred_fractional_scale = 0;
    if (wfs_visibility_draw_surf) {
        wp_fractional_scale_v1_destroy(wfs_visibility_draw_surf);
        wfs_visibility_draw_surf = NULL;
    }
    if (visibility_draw_surf_viewport) {
        wp_viewport_destroy(visibility_draw_surf_viewport);
        visibility_draw_surf_viewport = NULL;
    }
    if (visibility_layer_surface) {
        zwlr_layer_surface_v1_destroy(visibility_layer_surface);
        visibility_layer_surface = NULL;
    }
    if (visibility_draw_surf.surf) {
        drwsurf_reset(&visibility_draw_surf);
        wl_surface_destroy(visibility_draw_surf.surf);
        visibility_draw_surf.surf = NULL;
    }
}

void
flip_landscape()
{
    bool was_landscape = keyboard.landscape;

    reset_input_lifecycle(last_input_time);

    if (current_output) {
        keyboard.landscape = current_output->w > current_output->h;
    } else if (wl_outputs_size) {
        keyboard.landscape = wl_outputs[0].w > wl_outputs[0].h;
    }
    enum layout_id layer;
    if (keyboard.landscape) {
        layer = keyboard.landscape_layers[0];
        height = landscape_height;
    } else {
        layer = keyboard.layers[0];
        height = normal_height;
    }

    keyboard.layout = &keyboard.layouts[layer];
    keyboard.layer_index = 0;
    keyboard.prevlayout = keyboard.layout;
    keyboard.last_abc_layout = keyboard.layout;
    keyboard.last_abc_index = 0;

    if (layer_surface && was_landscape != keyboard.landscape) {
        destroy_keyboard_surfaces(last_input_time);
        show();
    }
}

void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t w, uint32_t h)
{
    double scale = keyboard.preferred_scale;

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    if (keyboard.preferred_fractional_scale) {
        scale = keyboard.preferred_fractional_scale;
    }

    if (keyboard.w != w || keyboard.h != h || keyboard.scale != scale ||
        keyboard_needs_configure) {

        keyboard.w = w;
        keyboard.h = h;
        keyboard.scale = scale;
        keyboard_needs_configure = false;
        if (wfs_mgr && viewporter) {
            wp_viewport_set_destination(draw_surf_viewport, keyboard.w,
                                        keyboard.h);
        } else {
            wl_surface_set_buffer_scale(draw_surf.surf, keyboard.scale);
        }

        destroy_popup_surface();

        popup_draw_surf.surf = wl_compositor_create_surface(compositor);

        xdg_positioner_set_size(popup_xdg_positioner, w, h * 2);
        xdg_positioner_set_anchor_rect(popup_xdg_positioner, 0, -h, w, h * 2);

        wl_surface_set_input_region(popup_draw_surf.surf, empty_region);
        popup_xdg_surface =
            xdg_wm_base_get_xdg_surface(wm_base, popup_draw_surf.surf);
        popup_xdg_surface_configured = false;
        xdg_surface_add_listener(popup_xdg_surface, &xdg_popup_surface_listener,
                                 NULL);
        popup_xdg_popup = xdg_surface_get_popup(popup_xdg_surface, NULL,
                                                popup_xdg_positioner);
        xdg_popup_add_listener(popup_xdg_popup, &xdg_popup_listener, NULL);
        zwlr_layer_surface_v1_get_popup(layer_surface, popup_xdg_popup);

        if (wfs_mgr && viewporter) {
            popup_draw_surf_viewport =
                wp_viewporter_get_viewport(viewporter, popup_draw_surf.surf);
            wp_viewport_set_destination(popup_draw_surf_viewport, keyboard.w,
                                        keyboard.h * 2);
        } else {
            wl_surface_set_buffer_scale(popup_draw_surf.surf, keyboard.scale);
        }

        wl_surface_commit(popup_draw_surf.surf);

        kbd_resize(&keyboard, layouts, NumLayouts);
        drwsurf_flip(&draw_surf);
    }
}

void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
    destroy_keyboard_surfaces(last_input_time);
    hide_visibility_control();
    visibility = VisibilityFullyHidden;
    run_display = false;
}

static void
visibility_surface_configure(void *data,
                             struct zwlr_layer_surface_v1 *surface,
                             uint32_t serial, uint32_t w, uint32_t h)
{
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    cancel_visibility_input();
    reset_input_lifecycle(last_input_time);
    visibility_configured = true;
    resize_visibility_control();
    draw_visibility_control(false);
}

static void
visibility_surface_closed(void *data,
                          struct zwlr_layer_surface_v1 *surface)
{
    hide_visibility_control();
    destroy_keyboard_surfaces(last_input_time);
    visibility = VisibilityFullyHidden;
    run_display = false;
}

void
usage(char *argv0)
{
    fprintf(stderr,
            "usage: %s [-hov] [-H height] [-L landscape height] [-fn font] [-l "
            "layers]\n",
            argv0);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -D          - Enable debug\n");
    fprintf(stderr, "  -o          - Print pressed keys to standard output\n");
    fprintf(stderr,
            "  -O          - Print intersected keys to standard output\n");
    fprintf(stderr,
            "  --mod-swipe - Tap, Ctrl/Alt/Ctrl+Alt swipes, or glide Latin "
            "letters\n");
    fprintf(stderr,
            "  --glide-learning-fd 3 - Send private swipe observations on fd 3\n");
    fprintf(stderr, "  -H [int]    - Height in pixels\n");
    fprintf(stderr, "  -L [int]    - Landscape height in pixels\n");
    fprintf(stderr, "  -R [int]    - Rounding radius in pixels\n");
    fprintf(stderr, "  --fn [font] - Set font (e.g: DejaVu Sans 20)\n");
    fprintf(stderr, "  --hidden    - Start hidden (send SIGUSR2 to show)\n");
    fprintf(
        stderr,
        "  --alpha [int]          - Set alpha value for all colors [0-255]\n");
    fprintf(stderr, "  --bg [rrggbb|aa]       - Set color of background\n");
    fprintf(stderr, "  --fg [rrggbb|aa]       - Set color of keys\n");
    fprintf(stderr, "  --fg-sp [rrggbb|aa]    - Set color of special keys\n");
    fprintf(stderr, "  --press [rrggbb|aa]     - Set color of pressed keys\n");
    fprintf(stderr,
            "  --press-sp [rrggbb|aa]  - Set color of pressed special keys\n");
    fprintf(stderr, "  --swipe [rrggbb|aa]    - Set color of swiped keys\n");
    fprintf(stderr,
            "  --swipe-sp [rrggbb|aa] - Set color of swiped special keys\n");
    fprintf(stderr, "  --text [rrggbb|aa]     - Set color of text on keys\n");
    fprintf(stderr,
            "  --text-sp [rrggbb|aa]  - Set color of text on special keys\n");
    fprintf(stderr,
            "  --list-layers          - Print the list of available layers\n");
    fprintf(stderr,
            "  -l                     - Comma separated list of layers\n");
    fprintf(stderr, "  --landscape-layers     - Comma separated list of "
                    "landscape layers\n");
}

void
list_layers()
{
    int i;
    for (i = 0; i < NumLayouts - 1; i++) {
        if (layouts[i].name) {
            puts(layouts[i].name);
        }
    }
}

void
hide()
{
    visibility_input = (struct visibility_input){0};
    destroy_keyboard_surfaces(last_input_time);
    hide_visibility_control();
    visibility = VisibilityFullyHidden;
}

static void
collapse()
{
    if (visibility != VisibilityExpanded || !layer_surface) {
        return;
    }
    destroy_keyboard_surfaces(last_input_time);
    visibility = VisibilityCollapsed;
    show_visibility_control();
}

void
show()
{
    cancel_visibility_input();
    if (layer_surface) {
        reset_input_lifecycle(last_input_time);
        visibility = VisibilityExpanded;
        show_visibility_control();
        return;
    }

    visibility = VisibilityExpanded;
    flip_landscape();

    keyboard_needs_configure = true;

    draw_surf.surf = wl_compositor_create_surface(compositor);
    wl_surface_add_listener(draw_surf.surf, &surface_listener, NULL);
    if (wfs_mgr && viewporter) {
        wfs_draw_surf = wp_fractional_scale_manager_v1_get_fractional_scale(
            wfs_mgr, draw_surf.surf);
        wp_fractional_scale_v1_add_listener(
            wfs_draw_surf, &wp_fractional_scale_listener, NULL);
        draw_surf_viewport =
            wp_viewporter_get_viewport(viewporter, draw_surf.surf);
    }

    layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, draw_surf.surf, selected_output(), layer, namespace);

    zwlr_layer_surface_v1_set_size(layer_surface, 0, height);
    zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface, false);
    zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener,
                                       NULL);
    wl_surface_commit(draw_surf.surf);
    show_visibility_control();
}

void
toggle_visibility()
{
    if (visibility == VisibilityFullyHidden)
        show();
    else
        hide();
}

static void
cancel_active_input(uint32_t time)
{
    struct mod_swipe_result result;

    kbd_clear_candidates(&keyboard);
    cur_press = false;
    cur_button = 0;
    cur_touch_id = -1;
    cur_x = cur_y = -1;
    if (mod_swipe_enabled && mod_swipe_cancel(&mod_swipe, &result)) {
        if (result.deferred) {
            finish_deferred_gesture();
        } else {
            kbd_release_key(&keyboard, result.time);
        }
    }
    if (keyboard.last_press) {
        kbd_release_key(&keyboard, time);
    }
    if (keyboard.last_popup_w && keyboard.last_popup_h) {
        kbd_clear_last_popup(&keyboard);
        drwsurf_flip(keyboard.popup_surf);
    }
}

static void
reset_input_lifecycle(uint32_t time)
{
    bool redraw_layout = keyboard.mods || keyboard.compose;

    glide_learning_clear(&glide_learning);
    kbd_clear_glide_undo(&keyboard);
    cancel_active_input(time);
    if (keyboard.mods) {
        keyboard.mods = NoMod;
        if (keyboard.vkbd) {
            zwp_virtual_keyboard_v1_modifiers(keyboard.vkbd, NoMod, 0, 0, 0);
        }
    }
    if (keyboard.compose) {
        keyboard.compose = 0;
    }
    if (redraw_layout && layer_surface && draw_surf.buf) {
        kbd_draw_layout(&keyboard);
        drwsurf_flip(&draw_surf);
    }
}

void
pipewarn()
{
    fprintf(stderr, "wvkbd: cannot pipe data out.\n");
}

void
set_kbd_colors(uint8_t *bgra, char *hex)
{
    // bg, fg, text, high, swipe
    int length = strlen(hex);
    if (length == 6 || length == 8) {
        char subhex[3] = {0};
        memcpy(subhex, hex, 2);
        bgra[2] = (int)strtol(subhex, NULL, 16);
        memcpy(subhex, hex + 2, 2);
        bgra[1] = (int)strtol(subhex, NULL, 16);
        memcpy(subhex, hex + 4, 2);
        bgra[0] = (int)strtol(subhex, NULL, 16);
        if (length == 8) {
            memcpy(subhex, hex + 6, 2);
            bgra[3] = (int)strtol(subhex, NULL, 16);
        }
    }
}

int
main(int argc, char **argv)
{
    /* parse command line arguments */
    char *layer_names_list = NULL, *landscape_layer_names_list = NULL;
    char *fc_font_pattern = NULL;
    int glide_learning_fd = -1;
    height = landscape_height = KBD_PIXEL_LANDSCAPE_HEIGHT;
    normal_height = KBD_PIXEL_HEIGHT;

    char *tmp;
    if ((tmp = getenv("WVKBD_LAYERS")))
        layer_names_list = estrdup(tmp);
    if ((tmp = getenv("WVKBD_LANDSCAPE_LAYERS")))
        landscape_layer_names_list = estrdup(tmp);
    if ((tmp = getenv("WVKBD_HEIGHT")))
        normal_height = atoi(tmp);
    if ((tmp = getenv("WVKBD_LANDSCAPE_HEIGHT")))
        landscape_height = atoi(tmp);

    /* keyboard settings */
    keyboard.layers = (enum layout_id *)&layers;
    keyboard.landscape_layers = (enum layout_id *)&landscape_layers;
    keyboard.schemes = schemes;
    keyboard.landscape = true;
    keyboard.layer_index = 0;
    keyboard.preferred_scale = 1;
    keyboard.preferred_fractional_scale = 0;

    uint8_t alpha = 0;
    bool alpha_defined = false;

    int i;
    for (i = 1; argv[i]; i++) {
        if ((!strcmp(argv[i], "-v")) || (!strcmp(argv[i], "--version"))) {
            printf("wvkbd-%s\n", VERSION);
            exit(0);
        } else if ((!strcmp(argv[i], "-h")) || (!strcmp(argv[i], "--help"))) {
            usage(argv[0]);
            exit(0);
        } else if (!strcmp(argv[i], "-l")) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            if (layer_names_list)
                free(layer_names_list);
            layer_names_list = estrdup(argv[++i]);
        } else if ((!strcmp(argv[i], "-landscape-layers")) ||
                   (!strcmp(argv[i], "--landscape-layers"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            if (landscape_layer_names_list)
                free(landscape_layer_names_list);
            landscape_layer_names_list = estrdup(argv[++i]);
        } else if ((!strcmp(argv[i], "-bg")) || (!strcmp(argv[i], "--bg"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[0].bg.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-alpha")) ||
                   (!strcmp(argv[i], "--alpha"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            alpha = atoi(argv[++i]);
            alpha_defined = true;
        } else if ((!strcmp(argv[i], "-fg")) || (!strcmp(argv[i], "--fg"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[0].fg.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-fg-sp")) ||
                   (!strcmp(argv[i], "--fg-sp"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[1].fg.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-press")) ||
                   (!strcmp(argv[i], "--press"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[0].high.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-press-sp")) ||
                   (!strcmp(argv[i], "--press-sp"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[1].high.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-swipe")) ||
                   (!strcmp(argv[i], "--swipe"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[0].swipe.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-swipe-sp")) ||
                   (!strcmp(argv[i], "--swipe-sp"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[1].swipe.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-text")) ||
                   (!strcmp(argv[i], "--text"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[0].text.bgra, argv[++i]);
        } else if ((!strcmp(argv[i], "-text-sp")) ||
                   (!strcmp(argv[i], "--text-sp"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            set_kbd_colors(keyboard.schemes[1].text.bgra, argv[++i]);
        } else if (!strcmp(argv[i], "-H")) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            normal_height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-L")) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            height = landscape_height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-R")) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            rounding = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-D")) {
            keyboard.debug = true;
        } else if ((!strcmp(argv[i], "-fn")) || (!strcmp(argv[i], "--fn"))) {
            if (i >= argc - 1) {
                usage(argv[0]);
                exit(1);
            }
            fc_font_pattern = estrdup(argv[++i]);
        } else if (!strcmp(argv[i], "-o")) {
            keyboard.print = true;
        } else if (!strcmp(argv[i], "-O")) {
            keyboard.print_intersect = true;
        } else if (!strcmp(argv[i], "--mod-swipe")) {
            mod_swipe_enabled = true;
        } else if (!strcmp(argv[i], "--glide-learning-fd")) {
            if (i >= argc - 1 || strcmp(argv[++i], "3")) {
                usage(argv[0]);
                exit(1);
            }
            glide_learning_fd = 3;
        } else if ((!strcmp(argv[i], "-hidden")) ||
                   (!strcmp(argv[i], "--hidden"))) {
            visibility = VisibilityFullyHidden;
        } else if ((!strcmp(argv[i], "-list-layers")) ||
                   (!strcmp(argv[i], "--list-layers"))) {
            list_layers();
            exit(0);
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            usage(argv[0]);
            exit(1);
        }
    }

    if (mod_swipe_enabled && keyboard.print_intersect) {
        die("--mod-swipe cannot be combined with -O\n");
    }
    if (glide_learning_fd >= 0) {
        glide_learning_sink_init(&glide_learning, glide_learning_fd);
    }

    if (alpha_defined) {
        keyboard.schemes[0].bg.bgra[3] = alpha;
        keyboard.schemes[0].fg.bgra[3] = alpha;
        keyboard.schemes[0].high.bgra[3] = alpha;
        keyboard.schemes[1].bg.bgra[3] = alpha;
        keyboard.schemes[1].fg.bgra[3] = alpha;
        keyboard.schemes[1].high.bgra[3] = alpha;
    }

    if (fc_font_pattern) {
        for (i = 0; i < countof(schemes); i++)
            schemes[i].font = fc_font_pattern;
    }

    if (rounding != DEFAULT_ROUNDING) {
        for (i = 0; i < countof(schemes); i++)
            schemes[i].rounding = rounding;
    }

    display = wl_display_connect(NULL);
    if (display == NULL) {
        die("Failed to create display\n");
    }

    draw_surf.ctx = &draw_ctx;
    popup_draw_surf.ctx = &draw_ctx;
    visibility_draw_surf.ctx = &draw_ctx;
    keyboard.surf = &draw_surf;
    keyboard.popup_surf = &popup_draw_surf;

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (compositor == NULL) {
        die("wl_compositor not available\n");
    }
    if (draw_ctx.shm == NULL) {
        die("wl_shm not available\n");
    }
    if (layer_shell == NULL) {
        die("layer_shell not available\n");
    }
    if (wm_base == NULL) {
        die("wm_base not available\n");
    }
    if (vkbd_mgr == NULL) {
        die("virtual_keyboard_manager not available\n");
    }

    // A second round-trip to receive wl_outputs events
    wl_display_roundtrip(display);

    empty_region = wl_compositor_create_region(compositor);
    popup_xdg_positioner = xdg_wm_base_create_positioner(wm_base);

    keyboard.vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkbd_mgr, seat);
    if (keyboard.vkbd == NULL) {
        die("failed to init virtual keyboard_manager\n");
    }

    kbd_init(&keyboard, (struct layout *)&layouts, layer_names_list,
             landscape_layer_names_list);
    keyboard.learning = &glide_learning;

    for (i = 0; i < countof(schemes); i++) {
        schemes[i].font_description =
            pango_font_description_from_string(schemes[i].font);
    }

    if (visibility != VisibilityFullyHidden)
        show();

    struct pollfd fds[2];
    int WAYLAND_FD = 0;
    int SIGNAL_FD = 1;
    fds[WAYLAND_FD].events = POLLIN;
    fds[SIGNAL_FD].events = POLLIN;

    fds[WAYLAND_FD].fd = wl_display_get_fd(display);
    if (fds[WAYLAND_FD].fd == -1) {
        die("Failed to get wayland_fd: %d\n", errno);
    }

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGUSR1);
    sigaddset(&signal_mask, SIGUSR2);
    sigaddset(&signal_mask, SIGRTMIN);
    sigaddset(&signal_mask, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &signal_mask, NULL) == -1) {
        die("Failed to disable handled signals: %d\n", errno);
    }

    fds[SIGNAL_FD].fd = signalfd(-1, &signal_mask, 0);
    if (fds[SIGNAL_FD].fd == -1) {
        die("Failed to get signalfd: %d\n", errno);
    }

    while (run_display) {
        wl_display_flush(display);
        poll(fds, 2, -1);

        if (fds[WAYLAND_FD].revents & POLLIN)
            wl_display_dispatch(display);
        if (fds[WAYLAND_FD].revents & POLLERR) {
            die("Exceptional condition on wayland socket.\n");
        }
        if (fds[WAYLAND_FD].revents & POLLHUP) {
            die("Wayland socket has been disconnected.\n");
        }

        if (fds[SIGNAL_FD].revents & POLLIN) {
            struct signalfd_siginfo si;

            if (read(fds[SIGNAL_FD].fd, &si, sizeof(si)) != sizeof(si))
                fprintf(stderr, "Signal read error: %d", errno);
            else if (si.ssi_signo == SIGUSR1)
                hide();
            else if (si.ssi_signo == SIGUSR2)
                show();
            else if (si.ssi_signo == SIGRTMIN)
                toggle_visibility();
            else if (si.ssi_signo == SIGPIPE)
                pipewarn();
        }
    }

    if (fc_font_pattern) {
        free((void *)fc_font_pattern);
        for (i = 0; i < countof(schemes); i++)
            schemes[i].font = NULL;
    }

    return 0;
}
