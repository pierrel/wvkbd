#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client-core.h>

#include "letters.h"
#include "keyboard.h"
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
    struct recorded_event *event = &events[event_count++];

    (void)proxy;
    (void)interface;
    (void)version;
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
    assert(kbd_emit_ascii_word(&keyboard, "abc", 3, 42));
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
    assert(kbd_emit_ascii_word(&keyboard, "az", 2, 9));
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
        assert(kbd_emit_ascii_word(&keyboard, &letter, 1, 7));
        assert(event_count == 5);
        expect_pair(1, code);
    }
    reset_events();
    assert(!kbd_emit_ascii_word(&keyboard, "a-", 2, 7));
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
    assert(kbd_emit_ascii_word(&keyboard, "abc", 3, 42));
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

    keyboard.glide_undo_count = 4;
    reset_events();
    assert(!kbd_begin_glide_followup(&keyboard, &digit, 9));
    assert(keyboard.glide_undo_count == 0);
    assert(event_count == 0);

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

int
main(void)
{
    test_batch_emission();
    test_capslock_and_full_letter_map();
    test_capslock_shift_xor();
    test_glide_undo();
    test_glide_admission();
    puts("keyboard glide tests passed");
    return 0;
}
