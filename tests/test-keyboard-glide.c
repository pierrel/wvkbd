#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client-core.h>

#include "letters.h"
#include "keyboard.h"
#include "glide-learning.h"
#include "proto/virtual-keyboard-unstable-v1-client-protocol.h"

struct recorded_event {
    uint32_t opcode;
    uint32_t first;
    uint32_t second;
    uint32_t third;
};

static struct recorded_event events[80];
static size_t event_count;

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
    va_list arguments;
    struct recorded_event *event;

    (void)interface;
    (void)version;
    if (proxy != (struct wl_proxy *)(uintptr_t)1) {
        return NULL;
    }
    assert(event_count < sizeof(events) / sizeof(events[0]));
    event = &events[event_count++];
    event->opcode = opcode;
    va_start(arguments, flags);
    event->first = va_arg(arguments, uint32_t);
    event->second = va_arg(arguments, uint32_t);
    event->third = va_arg(arguments, uint32_t);
    if (opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS) {
        (void)va_arg(arguments, uint32_t);
    }
    va_end(arguments);
    return NULL;
}

static void
reset_events(void)
{
    event_count = 0;
    memset(events, 0, sizeof(events));
}

static void
expect_pair(size_t index, uint32_t code)
{
    assert(events[index].opcode == ZWP_VIRTUAL_KEYBOARD_V1_KEY);
    assert(events[index].second == code);
    assert(events[index].third == WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(events[index + 1].opcode == ZWP_VIRTUAL_KEYBOARD_V1_KEY);
    assert(events[index + 1].second == code);
    assert(events[index + 1].third == WL_KEYBOARD_KEY_STATE_RELEASED);
}

static bool
commit_word(struct kbd *keyboard, const char *word, size_t length,
            uint32_t time)
{
    struct glide_result result = {
        .count = 1,
        .matches = {{.word = word, .length = length}},
    };

    return kbd_commit_glide_result(keyboard, &result, "abc", 3, time);
}

static void
test_batch_emission(void)
{
    struct key sentinel = {0};
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .mods = Shift,
        .last_press = &sentinel,
        .last_swipe = &sentinel,
    };
    int pipefd[2];
    int saved_stdout;
    char printed[8] = {0};

    assert(pipe(pipefd) == 0);
    saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdout >= 0);
    assert(dup2(pipefd[1], STDOUT_FILENO) >= 0);
    close(pipefd[1]);
    keyboard.print = true;
    reset_events();
    assert(commit_word(&keyboard, "abc", 3, 42));
    fflush(stdout);
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    assert(read(pipefd[0], printed, sizeof(printed)) == 4);
    close(pipefd[0]);

    assert(memcmp(printed, "Abc ", 4) == 0);
    assert(event_count == 10);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == Shift);
    expect_pair(1, KEY_A);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == 0);
    expect_pair(4, KEY_B);
    expect_pair(6, KEY_C);
    expect_pair(8, KEY_SPACE);
    assert(keyboard.mods == 0);
    assert(keyboard.glide_undo_count == 4);
    assert(keyboard.last_press == &sentinel);
    assert(keyboard.last_swipe == &sentinel);
}

static void
test_capslock_and_full_letter_map(void)
{
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .mods = CapsLock,
    };

    reset_events();
    assert(commit_word(&keyboard, "az", 2, 9));
    assert(event_count == 7);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == CapsLock);
    expect_pair(1, KEY_A);
    expect_pair(3, KEY_Z);
    expect_pair(5, KEY_SPACE);
    assert(keyboard.mods == CapsLock);

    for (char letter = 'a'; letter <= 'z'; letter++) {
        uint32_t code;

        assert(glide_letter_to_evdev(letter, &code));
        reset_events();
        assert(commit_word(&keyboard, &letter, 1, 7));
        assert(event_count == 5);
        expect_pair(1, code);
    }
    reset_events();
    assert(!commit_word(&keyboard, "a-", 2, 7));
    assert(event_count == 0);
}

static void
test_capslock_shift_xor(void)
{
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .mods = CapsLock | Shift,
        .print = true,
    };
    int pipefd[2];
    int saved_stdout;
    char printed[8] = {0};

    assert(pipe(pipefd) == 0);
    saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdout >= 0);
    assert(dup2(pipefd[1], STDOUT_FILENO) >= 0);
    close(pipefd[1]);
    assert(commit_word(&keyboard, "abc", 3, 42));
    fflush(stdout);
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    assert(read(pipefd[0], printed, sizeof(printed)) == 4);
    close(pipefd[0]);
    assert(memcmp(printed, "aBC ", 4) == 0);
    assert(keyboard.mods == CapsLock);
}

static void
test_glide_undo(void)
{
    struct key backspace = {.type = Code, .code = KEY_BACKSPACE};
    struct key shift = {.type = Mod, .code = Shift};
    struct key forced_backspace = {
        .type = Code,
        .code = KEY_BACKSPACE,
        .code_mod = Ctrl,
    };
    struct key space = {.type = Code, .code = KEY_SPACE};
    struct key comma = {.type = Code, .code = KEY_COMMA};
    struct key shifted_comma = {
        .type = Code,
        .code = KEY_COMMA,
        .code_mod = Shift,
    };
    struct key digit = {.type = Code, .code = KEY_1};
    struct key reset_digit = {
        .type = Code,
        .code = KEY_1,
        .reset_mod = true,
    };
    struct key shifted_digit = {
        .type = Code,
        .code = KEY_1,
        .code_mod = Shift,
    };
    struct key reset_shifted_digit = {
        .type = Code,
        .code = KEY_1,
        .code_mod = Shift,
        .reset_mod = true,
    };
    struct key other = {.type = Code, .code = KEY_A};
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .glide_undo_count = 4,
    };

    reset_events();
    kbd_press_key(&keyboard, &backspace, 9);
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 9);
    for (size_t i = 1; i < event_count; i += 2) {
        expect_pair(i, KEY_BACKSPACE);
    }
    keyboard.glide_undo_count = 4;
    assert(!kbd_begin_glide_followup(&keyboard, &other, 9));
    assert(keyboard.glide_undo_count == 0);
    keyboard.glide_undo_count = 4;
    keyboard.mods = Ctrl;
    assert(!kbd_begin_glide_followup(&keyboard, &backspace, 9));
    assert(keyboard.glide_undo_count == 0);

    keyboard.mods = 0;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &forced_backspace, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &reset_digit, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

    keyboard.glide_undo_count = 4;
    kbd_press_key(&keyboard, &space, 9);
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

    keyboard.glide_undo_count = 4;
    assert(!kbd_begin_glide_followup(&keyboard, &comma, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 3);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    expect_pair(1, KEY_BACKSPACE);

    keyboard.mods = 0;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &digit, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &shift, 9));
    assert(keyboard.glide_undo_count == 4);
    assert(event_count == 0);

    keyboard.mods = Shift;
    assert(!kbd_begin_glide_followup(&keyboard, &digit, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == 0);
    expect_pair(1, KEY_BACKSPACE);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == Shift);

    keyboard.mods = Shift;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &shifted_comma, 9));
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == 0);
    expect_pair(1, KEY_BACKSPACE);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == Shift);

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &reset_digit, 9));
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == 0);
    expect_pair(1, KEY_BACKSPACE);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == Shift);

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &digit, 9));
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == 0);
    expect_pair(1, KEY_BACKSPACE);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == Shift);

    keyboard.mods = 0;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &shifted_digit, 9));
    assert(event_count == 3);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    expect_pair(1, KEY_BACKSPACE);

    keyboard.mods = 0;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &reset_shifted_digit, 9));
    assert(event_count == 3);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    expect_pair(1, KEY_BACKSPACE);

    keyboard.mods = Shift;
    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &shifted_digit, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &reset_shifted_digit, 9));
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == 0);
    expect_pair(1, KEY_BACKSPACE);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[3].first == Shift);
}

static void
test_glide_admission(void)
{
    struct layout latin = {.abc = true, .keymap_name = "latin"};
    struct layout non_latin = {.abc = true, .keymap_name = "other"};
    struct kbd keyboard = {.layout = &latin};

    for (char expected = 'a'; expected <= 'z'; expected++) {
        char letter;
        uint32_t code;

        assert(glide_letter_to_evdev(expected, &code));
        struct key key = {.type = Code, .code = code, .layout = &non_latin};
        assert(kbd_glide_letter(&keyboard, &key, &letter));
        assert(letter == expected);
    }
    struct key latin_target = {.type = Code, .code = KEY_A, .layout = &latin};
    struct key semicolon = {.type = Code, .code = KEY_SEMICOLON};
    struct key modifier = {.type = Mod, .code = KEY_A};

    keyboard.layout = &non_latin;
    assert(!kbd_glide_letter(&keyboard, &latin_target, NULL));
    keyboard.layout = &latin;
    assert(!kbd_glide_letter(&keyboard, &semicolon, NULL));
    assert(!kbd_glide_letter(&keyboard, &modifier, NULL));
    keyboard.compose = 1;
    assert(!kbd_glide_letter(&keyboard, &latin_target, NULL));
    keyboard.compose = 0;
    keyboard.mods = Ctrl | Alt | Super | AltGr;
    assert(!kbd_glide_letter(&keyboard, &latin_target, NULL));
}

struct candidate_fixture {
    cairo_surface_t *image;
    struct drwsurf surface;
    struct layout layout;
    struct clr_scheme schemes[1];
    struct kbd keyboard;
};

static void
candidate_fixture_init(struct candidate_fixture *fixture)
{
    static struct key terminal = {.type = Last};

    *fixture = (struct candidate_fixture){0};
    fixture->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 301, 120);
    assert(cairo_surface_status(fixture->image) == CAIRO_STATUS_SUCCESS);
    fixture->surface.cairo = cairo_create(fixture->image);
    assert(cairo_status(fixture->surface.cairo) == CAIRO_STATUS_SUCCESS);
    fixture->surface.layout = pango_cairo_create_layout(fixture->surface.cairo);
    fixture->surface.surf = (struct wl_surface *)(uintptr_t)2;
    fixture->layout.keys = &terminal;
    fixture->layout.keyheight = 60;
    fixture->schemes[0].rounding = 0;
    fixture->keyboard = (struct kbd){
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .w = 301,
        .h = 120,
        .layout = &fixture->layout,
        .surf = &fixture->surface,
        .popup_surf = &fixture->surface,
        .schemes = fixture->schemes,
    };
}

static void
candidate_fixture_destroy(struct candidate_fixture *fixture)
{
    g_object_unref(fixture->surface.layout);
    cairo_destroy(fixture->surface.cairo);
    cairo_surface_destroy(fixture->image);
}

static void
test_control_alt_activation_is_balanced(void)
{
    struct candidate_fixture fixture;
    struct key key = {
        .label = "a",
        .shift_label = "A",
        .width = 1,
        .type = Code,
        .code = KEY_A,
        .w = 100,
        .h = 60,
    };

    candidate_fixture_init(&fixture);
    reset_events();
    kbd_activate_key(&fixture.keyboard, &key, 42, Ctrl | Alt);
    assert(event_count == 4);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[0].first == (Ctrl | Alt));
    assert(events[1].opcode == ZWP_VIRTUAL_KEYBOARD_V1_KEY);
    assert(events[1].second == KEY_A);
    assert(events[1].third == WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(events[2].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[2].first == 0);
    assert(events[3].opcode == ZWP_VIRTUAL_KEYBOARD_V1_KEY);
    assert(events[3].second == KEY_A);
    assert(events[3].third == WL_KEYBOARD_KEY_STATE_RELEASED);
    assert(fixture.keyboard.mods == 0);
    assert(fixture.keyboard.last_press == NULL);
    candidate_fixture_destroy(&fixture);
}

static struct glide_result
candidate_result(size_t count)
{
    static const char first[] = "hello";
    static const char second[] = "help";
    static const char third[] = "held";
    struct glide_result result = {
        .count = count,
        .matches =
            {
                {.word = first, .length = sizeof(first) - 1},
                {.word = second, .length = sizeof(second) - 1},
                {.word = third, .length = sizeof(third) - 1},
            },
    };

    return result;
}

static void
test_candidate_touch_replacement(void)
{
    struct candidate_fixture fixture;
    struct glide_result result = candidate_result(3);
    struct kbd *keyboard;

    candidate_fixture_init(&fixture);
    keyboard = &fixture.keyboard;
    keyboard->mods = Shift;
    reset_events();
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 9));
    assert(keyboard->candidates.count == 3);
    assert(keyboard->candidates.case_mods == Shift);
    assert(keyboard->mods == 0);
    assert(keyboard->glide_undo_count == 6);

    reset_events();
    assert(kbd_candidate_touch_down(keyboard, 41, 150, 30) ==
           KbdCandidateClaimed);
    assert(keyboard->candidates.owner == KbdCandidateOwnerTouch);
    assert(keyboard->candidates.pressed_slot == 1);
    assert(kbd_candidate_touch_up(keyboard, 99, 10) == KbdCandidateOwned);
    assert(keyboard->candidates.count == 3);
    assert(kbd_candidate_touch_motion(keyboard, 41, 250, 30) ==
           KbdCandidateOwned);
    assert(!keyboard->candidates.pressed_inside);
    assert(kbd_candidate_touch_up(keyboard, 41, 11) == KbdCandidateOwned);
    assert(keyboard->candidates.count == 0);
    assert(keyboard->glide_undo_count == 6);
    assert(event_count == 0);

    keyboard->mods = Shift;
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 12));
    reset_events();
    assert(kbd_candidate_touch_down(keyboard, 42, 150, 30) ==
           KbdCandidateClaimed);
    assert(kbd_candidate_touch_up(keyboard, 42, 13) == KbdCandidateOwned);
    assert(keyboard->candidates.count == 0);
    assert(keyboard->glide_undo_count == 5);
    assert(keyboard->mods == 0);
    assert(event_count == 25);
    assert(events[0].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    for (size_t i = 1; i < 13; i += 2) {
        expect_pair(i, KEY_BACKSPACE);
    }
    assert(events[13].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[13].first == Shift);
    expect_pair(14, KEY_H);
    assert(events[16].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
    assert(events[16].first == 0);
    expect_pair(17, KEY_E);
    expect_pair(19, KEY_L);
    expect_pair(21, KEY_P);
    expect_pair(23, KEY_SPACE);
    candidate_fixture_destroy(&fixture);
}

static void
test_replacement_and_exact_erase_emit_only_the_rejected_units(void)
{
    struct candidate_fixture fixture;
    struct glide_result result = candidate_result(3);
    struct glide_learning_sink learning = {.fd = -1};
    struct key backspace = {.type = Code, .code = KEY_BACKSPACE};
    char record[GLIDE_LEARNING_JSON_MAX];
    int sockets[2];
    ssize_t length;

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    learning.fd = sockets[0];
    candidate_fixture_init(&fixture);
    fixture.keyboard.learning = &learning;
    reset_events();
    assert(kbd_commit_glide_result(&fixture.keyboard, &result, "abc", 3, 1));
    assert(kbd_candidate_touch_down(&fixture.keyboard, 1, 150, 30) ==
           KbdCandidateClaimed);
    assert(kbd_candidate_touch_up(&fixture.keyboard, 1, 2) == KbdCandidateOwned);
    length = recv(sockets[1], record, sizeof(record) - 1, 0);
    assert(length > 0);
    record[length] = '\0';
    assert(strstr(record, "\"trace\":\"abc\""));
    assert(strstr(record, "\"word\":\"hello\""));
    reset_events();
    assert(kbd_begin_glide_followup(&fixture.keyboard, &backspace, 3));
    length = recv(sockets[1], record, sizeof(record) - 1, 0);
    assert(length > 0);
    record[length] = '\0';
    assert(strstr(record, "\"word\":\"help\""));
    assert(!kbd_begin_glide_followup(&fixture.keyboard, &backspace, 4));
    assert(fcntl(sockets[1], F_SETFL, O_NONBLOCK) == 0);
    assert(recv(sockets[1], record, sizeof(record), 0) < 0 && errno == EAGAIN);
    close(sockets[0]);
    close(sockets[1]);
    candidate_fixture_destroy(&fixture);
}

static void
test_candidate_hit_testing_and_pointer_ownership(void)
{
    struct candidate_fixture fixture;
    struct glide_result result = candidate_result(1);
    struct kbd *keyboard;

    candidate_fixture_init(&fixture);
    keyboard = &fixture.keyboard;
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 1));
    reset_events();
    assert(kbd_candidate_touch_down(keyboard, 1, 100, 30) ==
           KbdCandidateDismissed);
    assert(keyboard->candidates.count == 0);

    result.count = 2;
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 1));
    reset_events();
    assert(kbd_candidate_touch_down(keyboard, 1, -1, 0) == KbdCandidateMiss);
    assert(keyboard->candidates.count == 0);
    assert(keyboard->glide_undo_count == 6);
    assert(kbd_candidate_touch_down(keyboard, 1, 0, -1) == KbdCandidateMiss);
    assert(kbd_candidate_touch_down(keyboard, 1, 301, 0) == KbdCandidateMiss);
    assert(kbd_candidate_touch_down(keyboard, 1, 0, 60) == KbdCandidateMiss);
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 1));
    reset_events();
    assert(kbd_candidate_touch_down(keyboard, 1, 250, 30) ==
           KbdCandidateDismissed);
    assert(keyboard->candidates.count == 0);
    assert(event_count == 0);

    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 2));
    reset_events();
    assert(kbd_candidate_pointer_button(keyboard, 272, true, 150, 30, 3) ==
           KbdCandidateClaimed);
    assert(keyboard->candidates.pressed_inside);
    assert(kbd_candidate_pointer_motion(keyboard, -1, -1) ==
           KbdCandidateOwned);
    assert(!keyboard->candidates.pressed_inside);
    assert(kbd_candidate_pointer_motion(keyboard, 150, 30) ==
           KbdCandidateOwned);
    assert(keyboard->candidates.pressed_inside);
    assert(kbd_candidate_pointer_button(keyboard, 273, false, 150, 30, 4) ==
           KbdCandidateOwned);
    assert(keyboard->candidates.count == 2);
    assert(kbd_candidate_pointer_button(keyboard, 272, false, 150, 30, 5) ==
           KbdCandidateOwned);
    assert(keyboard->candidates.count == 0);
    assert(keyboard->glide_undo_count == 5);

    result.count = GLIDE_MAX_MATCHES + 1;
    reset_events();
    assert(!kbd_commit_glide_result(keyboard, &result, "abc", 3, 6));
    assert(event_count == 0);
    assert(keyboard->candidates.count == 0);
    candidate_fixture_destroy(&fixture);
}

static void
test_candidate_clear_resets_owned_session(void)
{
    struct candidate_fixture fixture;
    struct glide_result result = candidate_result(3);
    struct kbd *keyboard;

    candidate_fixture_init(&fixture);
    keyboard = &fixture.keyboard;
    assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 1));
    assert(kbd_candidate_touch_down(keyboard, 41, 150, 30) ==
           KbdCandidateClaimed);
    assert(keyboard->candidates.owner == KbdCandidateOwnerTouch);

    reset_events();
    kbd_clear_candidates(keyboard);
    assert(keyboard->candidates.count == 0);
    assert(keyboard->candidates.owner == KbdCandidateOwnerNone);
    assert(!keyboard->candidates.pressed_inside);
    assert(event_count == 0);
    candidate_fixture_destroy(&fixture);
}

static void
test_candidate_zero_and_case_replacement(void)
{
    static const uint8_t cases[] = {0, Shift, CapsLock, Shift | CapsLock};
    struct candidate_fixture fixture;
    struct glide_result result = candidate_result(2);
    struct kbd *keyboard;

    candidate_fixture_init(&fixture);
    keyboard = &fixture.keyboard;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t live_mods = cases[i] & CapsLock;
        size_t word_start;

        keyboard->mods = cases[i];
        assert(kbd_commit_glide_result(keyboard, &result, "abc", 3, 1));
        assert(keyboard->mods == live_mods);
        reset_events();
        assert(kbd_candidate_touch_down(keyboard, 1, 10, 30) ==
               KbdCandidateClaimed);
        assert(kbd_candidate_touch_up(keyboard, 1, 2) == KbdCandidateOwned);
        word_start = 13;
        assert(events[word_start].opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS);
        assert(events[word_start].first == cases[i]);
        assert(keyboard->mods == live_mods);
        assert(keyboard->glide_undo_count == 6);
        assert(keyboard->candidates.count == 0);
    }
    candidate_fixture_destroy(&fixture);
}

static void
test_no_candidate_learning_choices_are_explicit_and_emit_no_keys(void)
{
    struct candidate_fixture fixture;
    struct glide_learning_sink learning = {
        .fd = -1,
        .session = "0123456789abcdef0123456789abcdef",
    };
    char record[GLIDE_LEARNING_JSON_MAX];
    int sockets[2];
    ssize_t length;

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    learning.fd = sockets[0];
    learning.pending_gesture = 1;
    candidate_fixture_init(&fixture);
    fixture.keyboard.learning = &learning;

    kbd_show_learning_choices(&fixture.keyboard);
    assert(fixture.keyboard.candidates.count == 2);
    assert(fixture.keyboard.candidates.learning_choices);
    reset_events();
    assert(kbd_candidate_touch_down(&fixture.keyboard, 1, 10, 30) ==
           KbdCandidateClaimed);
    assert(kbd_candidate_touch_up(&fixture.keyboard, 1, 1) == KbdCandidateOwned);
    assert(event_count == 0);
    length = recv(sockets[1], record, sizeof(record), 0);
    assert(length > 0 && (size_t)length < sizeof(record));
    record[length] = '\0';
    assert(strstr(record, "\"outcome\":\"explicit-user-misswipe\""));

    learning.pending_gesture = 2;
    kbd_show_learning_choices(&fixture.keyboard);
    assert(kbd_candidate_touch_down(&fixture.keyboard, 2, 250, 30) ==
           KbdCandidateClaimed);
    assert(kbd_candidate_touch_up(&fixture.keyboard, 2, 2) == KbdCandidateOwned);
    assert(event_count == 0);
    length = recv(sockets[1], record, sizeof(record), 0);
    assert(length > 0 && (size_t)length < sizeof(record));
    record[length] = '\0';
    assert(strstr(record, "\"outcome\":\"explicit-lookup-failure\""));

    learning.pending_gesture = 3;
    kbd_show_learning_choices(&fixture.keyboard);
    assert(kbd_candidate_touch_down(&fixture.keyboard, 3, 10, 400) ==
           KbdCandidateMiss);
    assert(fixture.keyboard.candidates.count == 0);
    assert(!learning.pending_gesture);

    learning.pending_gesture = 4;
    kbd_show_learning_choices(&fixture.keyboard);
    assert(kbd_candidate_touch_down(&fixture.keyboard, 4, 10, 30) ==
           KbdCandidateClaimed);
    assert(kbd_candidate_touch_motion(&fixture.keyboard, 4, 10, 400) ==
           KbdCandidateOwned);
    assert(kbd_candidate_touch_up(&fixture.keyboard, 4, 3) == KbdCandidateOwned);
    assert(fixture.keyboard.candidates.count == 0);
    assert(!learning.pending_gesture);
    assert(event_count == 0);

    close(sockets[0]);
    close(sockets[1]);
    candidate_fixture_destroy(&fixture);
}

static void
test_retracted_glide_resolves_on_next_text_key(void)
{
    struct key backspace = {.type = Code, .code = KEY_BACKSPACE};
    struct key capslock = {.type = Mod, .code = CapsLock};
    struct key chord_modifier = {.type = Mod, .code = Ctrl};
    struct key letter = {.type = Code, .code = KEY_A};
    struct key control_letter = {
        .type = Code,
        .code = KEY_A,
        .code_mod = Ctrl,
    };
    struct key arrow = {.type = Code, .code = KEY_LEFT};
    struct key layout = {.type = Layout};
    struct key compose = {.type = Compose};
    struct key copy = {.type = Copy, .code = 0x00e9, .code_mod = 0x00c9};
    const uint8_t chord_modifiers[] = {Ctrl, Alt, Super, AltGr};
    struct glide_learning_sink learning = {
        .fd = -1,
        .session = "0123456789abcdef0123456789abcdef",
        .pending_gesture = 1,
        .pending_has_candidates = true,
    };
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .glide_undo_count = 4,
        .learning = &learning,
    };
    char record[GLIDE_LEARNING_JSON_MAX];
    int sockets[2];
    ssize_t length;

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    learning.fd = sockets[0];
    assert(kbd_begin_glide_followup(&keyboard, &backspace, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(learning.correction_pending);
    assert(!kbd_begin_glide_followup(&keyboard, &capslock, 10));
    assert(learning.pending_gesture == 1);
    assert(learning.correction_pending);
    assert(!kbd_begin_glide_followup(&keyboard, &arrow, 10));
    assert(learning.pending_gesture == 1);
    assert(learning.correction_pending);
    assert(!kbd_begin_glide_followup(&keyboard, &control_letter, 10));
    assert(learning.pending_gesture == 1);
    assert(learning.correction_pending);
    for (size_t i = 0;
         i < sizeof(chord_modifiers) / sizeof(chord_modifiers[0]); i++) {
        assert(!kbd_begin_glide_followup(&keyboard, &chord_modifier, 10));
        keyboard.mods = chord_modifiers[i];
        assert(!kbd_begin_glide_followup(&keyboard, &letter, 10));
        assert(learning.pending_gesture == 1);
        assert(learning.correction_pending);
        keyboard.mods = NoMod;
    }
    assert(!kbd_begin_glide_followup(&keyboard, &layout, 10));
    assert(learning.pending_gesture == 1);
    assert(learning.correction_pending);
    assert(!kbd_begin_glide_followup(&keyboard, &compose, 10));
    keyboard.compose = 1;
    assert(!kbd_begin_glide_followup(&keyboard, &layout, 10));
    assert(learning.pending_gesture == 1);
    assert(learning.correction_pending);
    keyboard.compose = 2;
    assert(!kbd_begin_glide_followup(&keyboard, &copy, 10));
    length = recv(sockets[1], record, sizeof(record), 0);
    assert(length > 0 && (size_t)length < sizeof(record));
    record[length] = '\0';
    assert(strstr(record, "\"outcome\":\"manual-correction-ambiguous\""));
    assert(!learning.pending_gesture);
    close(sockets[0]);
    close(sockets[1]);
}

static void
test_retracted_glide_resolves_on_plain_code_text(void)
{
    struct key backspace = {.type = Code, .code = KEY_BACKSPACE};
    struct key letter = {.type = Code, .code = KEY_A};
    struct glide_learning_sink learning = {
        .fd = -1,
        .session = "0123456789abcdef0123456789abcdef",
        .pending_gesture = 1,
        .pending_has_candidates = true,
    };
    struct kbd keyboard = {
        .vkbd = (struct zwp_virtual_keyboard_v1 *)(uintptr_t)1,
        .glide_undo_count = 4,
        .learning = &learning,
    };
    char record[GLIDE_LEARNING_JSON_MAX];
    int sockets[2];
    ssize_t length;

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    learning.fd = sockets[0];
    assert(kbd_begin_glide_followup(&keyboard, &backspace, 9));
    assert(!kbd_begin_glide_followup(&keyboard, &letter, 10));
    length = recv(sockets[1], record, sizeof(record), 0);
    assert(length > 0 && (size_t)length < sizeof(record));
    record[length] = '\0';
    assert(strstr(record, "\"outcome\":\"manual-correction-ambiguous\""));
    assert(!learning.pending_gesture);
    close(sockets[0]);
    close(sockets[1]);
}

static void
test_modified_glide_followup_commits_top_candidate(void)
{
    struct key letter = {.type = Code, .code = KEY_A};
    struct glide_learning_sink learning = {
        .fd = -1,
        .session = "0123456789abcdef0123456789abcdef",
        .pending_gesture = 1,
        .pending_has_candidates = true,
    };
    struct kbd keyboard = {
        .mods = Ctrl,
        .glide_undo_count = 4,
        .learning = &learning,
    };
    char record[GLIDE_LEARNING_JSON_MAX];
    int sockets[2];
    ssize_t length;

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    learning.fd = sockets[0];
    assert(!kbd_begin_glide_followup(&keyboard, &letter, 9));
    assert(keyboard.glide_undo_count == 0);
    length = recv(sockets[1], record, sizeof(record), 0);
    assert(length > 0 && (size_t)length < sizeof(record));
    record[length] = '\0';
    assert(strstr(record, "\"outcome\":\"top-committed\""));
    assert(!learning.pending_gesture);
    close(sockets[0]);
    close(sockets[1]);
}

int
main(void)
{
    test_control_alt_activation_is_balanced();
    test_batch_emission();
    test_capslock_and_full_letter_map();
    test_capslock_shift_xor();
    test_glide_undo();
    test_glide_admission();
    test_candidate_touch_replacement();
    test_replacement_and_exact_erase_emit_only_the_rejected_units();
    test_candidate_hit_testing_and_pointer_ownership();
    test_candidate_clear_resets_owned_session();
    test_candidate_zero_and_case_replacement();
    test_no_candidate_learning_choices_are_explicit_and_emit_no_keys();
    test_retracted_glide_resolves_on_next_text_key();
    test_retracted_glide_resolves_on_plain_code_text();
    test_modified_glide_followup_commits_top_candidate();
    puts("keyboard glide tests passed");
    return 0;
}
