#include "proto/virtual-keyboard-unstable-v1-client-protocol.h"
#include <linux/input-event-codes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <ctype.h>
#include "keyboard.h"
#include "glide.h"
#include "glide-learning.h"
#include "letters.h"
#include "drw.h"
#include "os-compatibility.h"

#define MAX_LAYERS 25

/* lazy die macro */
#define die(...)                                                               \
    fprintf(stderr, __VA_ARGS__);                                              \
    exit(1)

#ifndef KEYMAP
#error "make sure to define KEYMAP"
#endif
#include KEYMAP

static void
kbd_reset_candidates(struct kbd *kb)
{
    kb->candidates = (struct kbd_candidate_session){0};
}

void
kbd_switch_layout(struct kbd *kb, struct layout *l, size_t layer_index)
{
    kbd_reset_candidates(kb);
    kb->prevlayout = kb->layout;
    if ((kb->layer_index != kb->last_abc_index) && (kb->layout->abc)) {
        kb->last_abc_layout = kb->layout;
        kb->last_abc_index = kb->layer_index;
    }
    kb->layer_index = layer_index;
    kb->layout = l;
    if (kb->debug)
        fprintf(stderr, "Switching to layout %s, layer_index %ld\n",
                kb->layout->name, layer_index);
    if (!l->keymap_name)
        fprintf(stderr, "Layout has no keymap!"); // sanity check
    if ((!kb->prevlayout) ||
        (strcmp(kb->prevlayout->keymap_name, kb->layout->keymap_name) != 0)) {
        fprintf(stderr, "Switching to keymap %s\n", kb->layout->keymap_name);
        create_and_upload_keymap(kb, kb->layout->keymap_name, 0, 0);
    }
    kbd_draw_layout(kb);
}

void
kbd_next_layer(struct kbd *kb, struct key *k, bool invert)
{
    size_t layer_index = kb->layer_index;
    if ((kb->mods & Ctrl) || (kb->mods & Alt) || (kb->mods & AltGr) ||
        ((bool)kb->compose)) {
        // with modifiers ctrl/alt/altgr: switch to the first layer
        layer_index = 0;
        kb->mods = 0;
    } else if ((kb->mods & Shift) || (kb->mods & CapsLock) || (invert)) {
        // with modifiers shift/capslock or invert set: switch to the previous
        // layout in the layer sequence
        if (layer_index > 0) {
            layer_index--;
        } else {
            size_t layercount = 0;
            for (size_t i = 0; layercount == 0; i++) {
                if (kb->landscape) {
                    if (kb->landscape_layers[i] == NumLayouts)
                        layercount = i;
                } else {
                    if (kb->layers[i] == NumLayouts)
                        layercount = i;
                }
            }
            layer_index = layercount - 1;
        }
        if (!invert)
            kb->mods ^= Shift;
    } else {
        // normal behaviour: switch to the next layout in the layer sequence
        layer_index++;
    }
    size_t layercount = 0;
    for (size_t i = 0; layercount == 0; i++) {
        if (kb->landscape) {
            if (kb->landscape_layers[i] == NumLayouts)
                layercount = i;
        } else {
            if (kb->layers[i] == NumLayouts)
                layercount = i;
        }
    }
    if (layer_index >= layercount) {
        if (kb->debug)
            fprintf(stderr, "wrapping layer_index back to start\n");
        layer_index = 0;
    }
    enum layout_id layer;
    if (kb->landscape) {
        layer = kb->landscape_layers[layer_index];
    } else {
        layer = kb->layers[layer_index];
    }
    if (((bool)kb->compose) && (k)) {
        kb->compose = 0;
        kbd_draw_key(kb, k, Unpress);
    }
    kbd_switch_layout(kb, &kb->layouts[layer], layer_index);
}

uint8_t
kbd_get_rows(struct layout *l)
{
    uint8_t rows = 0;
    struct key *k = l->keys;
    while (k->type != Last) {
        if (k->type == EndRow) {
            rows++;
        }
        k++;
    }
    return rows + 1;
}

enum layout_id *
kbd_init_layers(char *layer_names_list)
{
    enum layout_id *layers;
    uint8_t numlayers = 0;
    bool found;
    char *s;
    int i;

    layers = malloc(MAX_LAYERS * sizeof(enum layout_id));
    s = strtok(layer_names_list, ",");
    while (s != NULL) {
        if (numlayers + 1 == MAX_LAYERS) {
            fprintf(stderr, "too many layers specified");
            exit(3);
        }
        found = false;
        for (i = 0; i < NumLayouts - 1; i++) {
            if (layouts[i].name && strcmp(layouts[i].name, s) == 0) {
                fprintf(stderr, "layer #%d = %s\n", numlayers + 1, s);
                layers[numlayers++] = i;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "No such layer: %s\n", s);
            exit(3);
        }
        s = strtok(NULL, ",");
    }
    layers[numlayers] = NumLayouts; // mark the end of the sequence
    if (numlayers == 0) {
        fprintf(stderr, "No layers defined\n");
        exit(3);
    }

    return layers;
}

void
kbd_init(struct kbd *kb, struct layout *layouts, char *layer_names_list,
         char *landscape_layer_names_list)
{
    int i;

    fprintf(stderr, "Initializing keyboard\n");

    kb->layouts = layouts;

    for (i = 0; i < NumLayouts - 1; i++)
        ;
    fprintf(stderr, "Found %d layouts\n", i);

    kb->layer_index = 0;
    kb->last_abc_index = 0;

    if (layer_names_list)
        kb->layers = kbd_init_layers(layer_names_list);
    if (landscape_layer_names_list)
        kb->landscape_layers = kbd_init_layers(landscape_layer_names_list);

    i = 0;
    enum layout_id lid = kb->layers[0];
    while (lid != NumLayouts) {
        lid = kb->layers[++i];
    }
    fprintf(stderr, "Found %d layers\n", i);

    enum layout_id layer;
    if (kb->landscape) {
        layer = kb->landscape_layers[kb->layer_index];
    } else {
        layer = kb->layers[kb->layer_index];
    }

    kb->layout = &kb->layouts[layer];
    kb->last_abc_layout = &kb->layouts[layer];

    /* upload keymap */
    create_and_upload_keymap(kb, kb->layout->keymap_name, 0, 0);
}

void
kbd_init_layout(struct layout *l, uint32_t width, uint32_t height)
{
    uint32_t x = 0, y = 0;
    uint8_t rows = kbd_get_rows(l);

    l->keyheight = height / rows;

    struct key *k = l->keys;
    double rowlength = kbd_get_row_length(k);
    double rowwidth = 0.0;
    while (k->type != Last) {
        if (k->type == EndRow) {
            y += l->keyheight;
            x = 0;
            rowwidth = 0.0;
            rowlength = kbd_get_row_length(k + 1);
        } else if (k->width > 0) {
            k->x = x;
            k->y = y;
            k->w = ((double)width / rowlength) * k->width;
            x += k->w;
            rowwidth += k->width;
            if (x < (rowwidth / rowlength) * (double)width) {
                k->w++;
                x++;
            }
        }
        k->h = l->keyheight;
        k++;
    }
}

double
kbd_get_row_length(struct key *k)
{
    double l = 0.0;
    while ((k->type != Last) && (k->type != EndRow)) {
        l += k->width;
        k++;
    }
    return l;
}

struct key *
kbd_get_key(struct kbd *kb, uint32_t x, uint32_t y)
{
    struct layout *l = kb->layout;
    struct key *k = l->keys;
    if (kb->debug)
        fprintf(stderr, "get key: +%d+%d\n", x, y);
    while (k->type != Last) {
        if ((k->type != EndRow) && (k->type != Pad) && (k->type != Pad) &&
            (x >= k->x) && (y >= k->y) && (x < k->x + k->w) &&
            (y < k->y + k->h)) {
            return k;
        }
        k++;
    }
    return NULL;
}

size_t
kbd_get_layer_index(struct kbd *kb, struct layout *l)
{
    for (size_t i = 0; i < NumLayouts - 1; i++) {
        if (l == &kb->layouts[i]) {
            return i;
        }
    }
    return 0;
}

void
kbd_unpress_key(struct kbd *kb, uint32_t time)
{
    bool unlatch_shift, unlatch_ctrl, unlatch_alt, unlatch_super, unlatch_altgr;
    unlatch_shift = unlatch_ctrl = unlatch_alt = unlatch_super = unlatch_altgr =
        false;

    if (kb->last_press) {
        unlatch_shift = (kb->mods & Shift) == Shift;
        unlatch_ctrl = (kb->mods & Ctrl) == Ctrl;
        unlatch_alt = (kb->mods & Alt) == Alt;
        unlatch_super = (kb->mods & Super) == Super;
        unlatch_altgr = (kb->mods & AltGr) == AltGr;

        if (unlatch_shift)
            kb->mods ^= Shift;
        if (unlatch_ctrl)
            kb->mods ^= Ctrl;
        if (unlatch_alt)
            kb->mods ^= Alt;
        if (unlatch_super)
            kb->mods ^= Super;
        if (unlatch_altgr)
            kb->mods ^= AltGr;

        if (unlatch_shift || unlatch_ctrl || unlatch_alt || unlatch_super ||
            unlatch_altgr) {
            zwp_virtual_keyboard_v1_modifiers(kb->vkbd, kb->mods, 0, 0, 0);
        }

        if (kb->last_press->type == Copy) {
            zwp_virtual_keyboard_v1_key(kb->vkbd, time, 127, // COMP key
                                        WL_KEYBOARD_KEY_STATE_RELEASED);
        } else {
            if ((kb->last_press->code == KEY_SPACE) && (unlatch_shift)) {
                // shift + space is tab
                zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_TAB,
                                            WL_KEYBOARD_KEY_STATE_RELEASED);
            } else {
                zwp_virtual_keyboard_v1_key(kb->vkbd, time,
                                            kb->last_press->code,
                                            WL_KEYBOARD_KEY_STATE_RELEASED);
            }
        }

        if (kb->compose >= 2) {
            kb->compose = 0;
            kbd_switch_layout(kb, kb->last_abc_layout, kb->last_abc_index);
        } else if (unlatch_shift || unlatch_ctrl || unlatch_alt ||
                   unlatch_super || unlatch_altgr) {
            kbd_draw_layout(kb);
        } else {
            kbd_draw_key(kb, kb->last_press, Unpress);
        }

        kb->last_press = NULL;
    }
}

void
kbd_release_key(struct kbd *kb, uint32_t time)
{
    kbd_unpress_key(kb, time);
    if (kb->print_intersect && kb->last_swipe) {
        printf("\n");
        // Important so autocompleted words get typed in time
        fflush(stdout);
        kbd_draw_layout(kb);
        kb->last_swipe = NULL;
    }

    drwsurf_flip(kb->surf);

    kbd_clear_last_popup(kb);
    drwsurf_flip(kb->popup_surf);
}

void
kbd_motion_key(struct kbd *kb, uint32_t time, uint32_t x, uint32_t y)
{
    // Output intersecting keys
    // (for external 'swiping'-based accelerators).
    if (kb->print_intersect) {
        if (kb->last_press) {
            kbd_unpress_key(kb, time);
            // Redraw last press as a swipe.
            kbd_draw_key(kb, kb->last_swipe, Swipe);
        }
        struct key *intersect_key;
        intersect_key = kbd_get_key(kb, x, y);
        if (intersect_key && (!kb->last_swipe ||
                              intersect_key->label != kb->last_swipe->label)) {
            kbd_print_key_stdout(kb, intersect_key);
            kb->last_swipe = intersect_key;
            kbd_draw_key(kb, kb->last_swipe, Swipe);
        }
    } else {
        kbd_unpress_key(kb, time);
    }

    drwsurf_flip(kb->surf);

    kbd_clear_last_popup(kb);
    drwsurf_flip(kb->popup_surf);
}

static uint32_t
kbd_effective_modifiers(const struct kbd *kb, const struct key *key)
{
    if (!key->code_mod) {
        return kb->mods;
    }
    return key->reset_mod ? key->code_mod : kb->mods ^ key->code_mod;
}

void
kbd_press_key(struct kbd *kb, struct key *k, uint32_t time)
{
    if (kbd_begin_glide_followup(kb, k, time)) {
        return;
    }
    if ((kb->compose == 1) && (k->type != Compose) && (k->type != Mod)) {
        if ((k->type == NextLayer) || (k->type == BackLayer) ||
            ((k->type == Code) && (k->code == KEY_SPACE))) {
            kb->compose = 0;
            if (kb->debug)
                fprintf(stderr, "showing layout index\n");
            kbd_switch_layout(kb, &kb->layouts[Index], 0);
            return;
        } else if (k->layout) {
            kb->compose++;
            if (kb->debug)
                fprintf(stderr, "showing compose %d\n", kb->compose);
            kbd_switch_layout(kb, k->layout,
                              kbd_get_layer_index(kb, k->layout));
            return;
        } else {
            return;
        }
    }

    switch (k->type) {
    case Code:
        zwp_virtual_keyboard_v1_modifiers(
            kb->vkbd, kbd_effective_modifiers(kb, k), 0, 0, 0);
        kb->last_swipe = kb->last_press = k;
        kbd_draw_key(kb, k, Press);
        if ((k->code == KEY_SPACE) && (kb->mods & Shift)) {
            // shift space is tab
            zwp_virtual_keyboard_v1_modifiers(kb->vkbd, 0, 0, 0, 0);
            zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_TAB,
                                        WL_KEYBOARD_KEY_STATE_PRESSED);
        } else {
            zwp_virtual_keyboard_v1_key(kb->vkbd, time, kb->last_press->code,
                                        WL_KEYBOARD_KEY_STATE_PRESSED);
        }
        if (kb->print || kb->print_intersect)
            kbd_print_key_stdout(kb, k);
        if (kb->compose) {
            if (kb->debug)
                fprintf(stderr, "pressing composed key\n");
            kb->compose++;
        }
        break;
    case Mod:
        kb->mods ^= k->code;
        if ((k->code == Shift) || (k->code == CapsLock)) {
            kbd_draw_layout(kb);
        } else {
            if (kb->mods & k->code) {
                kbd_draw_key(kb, k, Press);
            } else {
                kbd_draw_key(kb, k, Unpress);
            }
        }
        zwp_virtual_keyboard_v1_modifiers(kb->vkbd, kb->mods, 0, 0, 0);
        break;
    case Layout:
        // switch to the layout determined by the key
        kbd_switch_layout(kb, k->layout, kbd_get_layer_index(kb, k->layout));
        // reset previous layout to default/first so we don't get any weird
        // cycles
        kb->last_abc_index = 0;
        if (kb->landscape) {
            kb->last_abc_layout = &kb->layouts[kb->landscape_layers[0]];
        } else {
            kb->last_abc_layout = &kb->layouts[kb->layers[0]];
        }
        break;
    case Compose:
        // switch to the associated layout determined by the *next* keypress
        if (kb->compose == 0) {
            kb->compose = 1;
        } else {
            kb->compose = 0;
        }
        if ((bool)kb->compose) {
            kbd_draw_key(kb, k, Press);
        } else {
            kbd_draw_key(kb, k, Unpress);
        }
        break;
    case NextLayer: //(also handles previous layer when shift modifier is on, or
                    //"first layer" with other modifiers)
        kbd_next_layer(kb, k, false);
        break;
    case BackLayer: // triggered when "Abc" keys are pressed
        // switch to the last active alphabetical layout
        if (kb->last_abc_layout) {
            kb->compose = 0;
            kbd_switch_layout(kb, kb->last_abc_layout, kb->last_abc_index);
            // reset previous layout to default/first so we don't get any weird
            // cycles
            kb->last_abc_index = 0;
            if (kb->landscape) {
                kb->last_abc_layout = &kb->layouts[kb->landscape_layers[0]];
            } else {
                kb->last_abc_layout = &kb->layouts[kb->layers[0]];
            }
        }
        break;
    case Copy:
        // copy code as unicode chr by setting a temporary keymap
        kb->last_swipe = kb->last_press = k;
        kbd_draw_key(kb, k, Press);
        if (kb->debug)
            fprintf(stderr, "pressing copy key\n");
        create_and_upload_keymap(kb, kb->layout->keymap_name, k->code,
                                 k->code_mod);
        zwp_virtual_keyboard_v1_modifiers(kb->vkbd, kb->mods, 0, 0, 0);
        zwp_virtual_keyboard_v1_key(kb->vkbd, time, 127, // COMP key
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        if (kb->print || kb->print_intersect)
            kbd_print_key_stdout(kb, k);
        break;
    default:
        break;
    }

    drwsurf_flip(kb->surf);
    drwsurf_flip(kb->popup_surf);
}

void
kbd_activate_key(struct kbd *kb, struct key *k, uint32_t time,
                 uint8_t transient_modifier)
{
    kb->mods |= transient_modifier;
    kbd_press_key(kb, k, time);
    kbd_release_key(kb, time);
}

void
kbd_print_key_stdout(struct kbd *kb, struct key *k)
{
    /* printed keys may slightly differ from the actual output
     * we generally print what is on the key LABEL and only support the normal
     * and shift layers. Other modifiers produce no output (Ctrl,Alt)
     * */

    bool handled = true;
    if (k->type == Code) {
        switch (k->code) {
        case KEY_SPACE:
            printf(" ");
            break;
        case KEY_ENTER:
            printf("\n");
            break;
        case KEY_BACKSPACE:
            printf("\b");
            break;
        case KEY_TAB:
            printf("\t");
            break;
        default:
            handled = false;
            break;
        }
    } else if (k->type != Copy) {
        return;
    }

    if (!handled) {
        if ((kb->mods & Shift) ||
            ((kb->mods & CapsLock) &
             (strlen(k->label) == 1 && isalpha(k->label[0]))))
            printf("%s", k->shift_label);
        else if (!(kb->mods & Ctrl) && !(kb->mods & Alt) && !(kb->mods & Super))
            printf("%s", k->label);
    }
    fflush(stdout);
}

void
kbd_clear_last_popup(struct kbd *kb)
{
    if (kb->last_popup_w && kb->last_popup_h) {
        drw_do_clear(kb->popup_surf, kb->last_popup_x, kb->last_popup_y,
                     kb->last_popup_w, kb->last_popup_h);
        wl_surface_damage(kb->popup_surf->surf, kb->last_popup_x,
                          kb->last_popup_y, kb->last_popup_w, kb->last_popup_h);

        kb->last_popup_w = kb->last_popup_h = 0;
    }
}

static const char *
kbd_key_label(struct kbd *kb, struct key *k)
{
    return ((kb->mods & Shift) ||
            ((kb->mods & CapsLock) && strlen(k->label) == 1 &&
             isalpha(k->label[0])))
               ? k->shift_label
               : k->label;
}

static void
kbd_draw_key_label(struct kbd *kb, struct key *k, enum key_draw_type type,
                   const char *label)
{
    if (kb->debug)
        fprintf(stderr, "Draw key +%d+%d %dx%d -> %s\n", k->x, k->y, k->w, k->h,
                label);
    struct clr_scheme *scheme = &kb->schemes[k->scheme];

    switch (type) {
    case None:
    case Unpress:
        draw_inset(kb->surf, k->x, k->y, k->w, k->h, KBD_KEY_BORDER, scheme->fg,
                   scheme->rounding);
        break;
    case Press:
        draw_inset(kb->surf, k->x, k->y, k->w, k->h, KBD_KEY_BORDER,
                   scheme->high, scheme->rounding);
        break;
    case Swipe:
        draw_over_inset(kb->surf, k->x, k->y, k->w, k->h, KBD_KEY_BORDER,
                        scheme->swipe, scheme->rounding);
        break;
    }

    drw_draw_text(kb->surf, scheme->text, k->x, k->y, k->w, k->h,
                  KBD_KEY_BORDER, label, scheme->font_description);
    wl_surface_damage(kb->surf->surf, k->x, k->y, k->w, k->h);

    if (type == Press || type == Unpress)
        kbd_show_popup_feedback(kb, k, label);
}

void
kbd_draw_key(struct kbd *kb, struct key *k, enum key_draw_type type)
{
    kbd_draw_key_label(kb, k, type, kbd_key_label(kb, k));
}

void
kbd_show_key_feedback(struct kbd *kb, struct key *k, const char *prefix)
{
    char feedback[64];
    const char *label = kbd_key_label(kb, k);

    if (prefix) {
        snprintf(feedback, sizeof(feedback), "%s%s", prefix, label);
        label = feedback;
    }
    kbd_draw_key_label(kb, k, Press, label);
    drwsurf_flip(kb->surf);
    drwsurf_flip(kb->popup_surf);
}

void
kbd_show_popup_feedback(struct kbd *kb, struct key *k, const char *label)
{
    struct clr_scheme *scheme = &kb->schemes[k->scheme];

    kbd_clear_last_popup(kb);
    kb->last_popup_x = k->x;
    kb->last_popup_y = kb->h + k->y - k->h;
    kb->last_popup_w = k->w;
    kb->last_popup_h = k->h;
    drw_fill_rectangle(kb->popup_surf, scheme->bg, k->x, kb->last_popup_y, k->w,
                       k->h, scheme->rounding);
    draw_inset(kb->popup_surf, k->x, kb->last_popup_y, k->w, k->h,
               KBD_KEY_BORDER, scheme->high, scheme->rounding);
    drw_draw_text(kb->popup_surf, scheme->text, k->x, kb->last_popup_y, k->w,
                  k->h, KBD_KEY_BORDER, label, scheme->font_description);
    wl_surface_damage(kb->popup_surf->surf, k->x, kb->last_popup_y, k->w, k->h);
}

void
kbd_clear_key_feedback(struct kbd *kb, struct key *k)
{
    kbd_draw_key(kb, k, Unpress);
    drwsurf_flip(kb->surf);
    kbd_clear_last_popup(kb);
    drwsurf_flip(kb->popup_surf);
}

bool
kbd_glide_letter(struct kbd *kb, const struct key *key, char *letter)
{
    return kb->compose == 0 && kb->layout && kb->layout->abc &&
           kb->layout->keymap_name &&
           strcmp(kb->layout->keymap_name, "latin") == 0 && key &&
           key->type == Code && !(kb->mods & (Ctrl | Alt | Super | AltGr)) &&
           glide_letter_from_evdev(key->code, letter);
}

bool
kbd_glide_geometry(const struct kbd *kb, struct glide_geometry *geometry)
{
    bool seen[26] = {0};
    struct key *key;

    if (!kb || !kb->layout || !geometry || kb->layout->keyheight == 0) {
        return false;
    }
    *geometry = (struct glide_geometry){0};
    key = kb->layout->keys;
    while (key->type != Last) {
        char letter;
        uint64_t x;
        uint64_t y;

        if (key->type != Code || !glide_letter_from_evdev(key->code, &letter)) {
            key++;
            continue;
        }
        if (seen[letter - 'a'] || key->w == 0 || key->h == 0) {
            return false;
        }
        x = (uint64_t)key->x + key->w / 2;
        y = (uint64_t)key->y + key->h / 2;
        if (x > INT32_MAX || y > INT32_MAX) {
            return false;
        }
        geometry->letters[letter - 'a'] =
            (struct glide_point){.x = (int32_t)x, .y = (int32_t)y};
        seen[letter - 'a'] = true;
        key++;
    }
    for (size_t i = 0; i < sizeof(seen) / sizeof(seen[0]); i++) {
        if (!seen[i]) {
            return false;
        }
    }
    geometry->key_height = kb->layout->keyheight;
    geometry->complete = true;
    return true;
}

bool
kbd_key_changes_interpretation(const struct kbd *kb, const struct key *key)
{
    if (!key) {
        return false;
    }
    if (kb->compose == 1 && key->type != Compose && key->type != Mod) {
        return true;
    }
    return key->type == Layout || key->type == NextLayer ||
           key->type == BackLayer || key->type == Copy;
}

static bool
kbd_ascii_word_codes(const char *word, size_t length,
                     uint32_t codes[GLIDE_MAX_WORD])
{
    if (!word || length == 0 || length > GLIDE_MAX_WORD) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!glide_letter_to_evdev(word[i], &codes[i])) {
            return false;
        }
    }
    return true;
}

static char
kbd_case_letter(char letter, size_t index, uint8_t case_mods)
{
    bool uppercase =
        (index == 0 && (case_mods & Shift)) != ((case_mods & CapsLock) != 0);

    return uppercase ? toupper((unsigned char)letter) : letter;
}

static void
kbd_emit_ascii_word_case(struct kbd *kb, const char *word, size_t length,
                         const uint32_t codes[GLIDE_MAX_WORD], uint32_t time,
                         uint8_t case_mods, bool consume_live_shift)
{
    char printed[GLIDE_MAX_WORD];
    uint8_t emitted_mods = case_mods;
    bool shifted = (case_mods & Shift) != 0;

    zwp_virtual_keyboard_v1_modifiers(kb->vkbd, emitted_mods, 0, 0, 0);
    for (size_t i = 0; i < length; i++) {
        zwp_virtual_keyboard_v1_key(kb->vkbd, time, codes[i],
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(kb->vkbd, time, codes[i],
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
        if (i == 0 && shifted) {
            emitted_mods &= ~Shift;
            if (consume_live_shift) {
                kb->mods &= ~Shift;
            }
            zwp_virtual_keyboard_v1_modifiers(kb->vkbd, emitted_mods, 0, 0, 0);
        }
    }
    zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_SPACE,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_SPACE,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    if (kb->print) {
        for (size_t i = 0; i < length; i++) {
            printed[i] = kbd_case_letter(word[i], i, case_mods);
        }
        fwrite(printed, 1, length, stdout);
        fputc(' ', stdout);
        fflush(stdout);
    }
    if (!consume_live_shift && emitted_mods != kb->mods) {
        zwp_virtual_keyboard_v1_modifiers(kb->vkbd, kb->mods, 0, 0, 0);
    }
    kb->glide_undo_count = (uint8_t)(length + 1);
}

void
kbd_clear_glide_undo(struct kbd *kb)
{
    kb->glide_undo_count = 0;
}

static void
kbd_emit_backspaces(struct kbd *kb, uint8_t count, uint32_t time,
                    bool restore_mods)
{
    zwp_virtual_keyboard_v1_modifiers(kb->vkbd, 0, 0, 0, 0);
    for (uint8_t i = 0; i < count; i++) {
        zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_BACKSPACE,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(kb->vkbd, time, KEY_BACKSPACE,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    if (kb->print) {
        for (uint8_t i = 0; i < count; i++) {
            fputc('\b', stdout);
        }
        fflush(stdout);
    }
    if (restore_mods && kb->mods) {
        zwp_virtual_keyboard_v1_modifiers(kb->vkbd, kb->mods, 0, 0, 0);
    }
}

static void
kbd_redraw_candidates(struct kbd *kb)
{
    kbd_draw_layout(kb);
    drwsurf_flip(kb->surf);
}

void
kbd_clear_candidates(struct kbd *kb)
{
    if (!kb->candidates.count) {
        return;
    }
    kbd_reset_candidates(kb);
    kbd_redraw_candidates(kb);
}

void
kbd_show_learning_choices(struct kbd *kb)
{
    kbd_reset_candidates(kb);
    kb->candidates.count = 2;
    kb->candidates.learning_choices = true;
    kbd_redraw_candidates(kb);
}

bool
kbd_commit_glide_result(struct kbd *kb, const struct glide_result *result,
                        uint32_t time)
{
    uint32_t codes[GLIDE_MAX_MATCHES][GLIDE_MAX_WORD];
    uint8_t case_mods;

    if (!result || result->count == 0 || result->count > GLIDE_MAX_MATCHES) {
        return false;
    }
    for (size_t i = 0; i < result->count; i++) {
        if (!kbd_ascii_word_codes(result->matches[i].word,
                                  result->matches[i].length, codes[i])) {
            return false;
        }
    }
    case_mods = kb->mods & (Shift | CapsLock);
    kbd_emit_ascii_word_case(kb, result->matches[0].word,
                             result->matches[0].length, codes[0], time,
                             case_mods, true);
    kbd_reset_candidates(kb);
    kb->candidates.count = result->count;
    memcpy(kb->candidates.matches, result->matches,
           result->count * sizeof(result->matches[0]));
    kb->candidates.case_mods = case_mods;
    return true;
}

static uint32_t
kbd_candidate_boundary(const struct kbd *kb, size_t slot)
{
    size_t slots = kb->candidates.learning_choices ? 2 : GLIDE_MAX_MATCHES;

    return (uint32_t)((uint64_t)slot * kb->w / slots);
}

static int
kbd_candidate_slot(const struct kbd *kb, int32_t x, int32_t y)
{

    if (!kb->candidates.count || !kb->layout || kb->w == 0 || x < 0 || y < 0 ||
        (uint32_t)x >= kb->w || (uint32_t)y >= kb->layout->keyheight) {
        return -1;
    }
    size_t slots = kb->candidates.learning_choices ? 2 : GLIDE_MAX_MATCHES;
    for (size_t slot = 1; slot < slots; slot++) {
        if ((uint32_t)x < kbd_candidate_boundary(kb, slot)) {
            return (int)slot - 1;
        }
    }
    return (int)slots - 1;
}

static enum kbd_candidate_event
kbd_candidate_begin(struct kbd *kb, enum kbd_candidate_owner owner,
                    int32_t touch_id, uint32_t pointer_button, int32_t x,
                    int32_t y)
{
    int slot;

    if (!kb->candidates.count) {
        return KbdCandidateMiss;
    }
    if (kb->candidates.owner != KbdCandidateOwnerNone) {
        return KbdCandidateOwned;
    }
    slot = kbd_candidate_slot(kb, x, y);
    if (slot < 0) {
        if (kb->candidates.learning_choices) {
            glide_learning_clear(kb->learning);
        }
        kbd_reset_candidates(kb);
        kbd_redraw_candidates(kb);
        return KbdCandidateMiss;
    }
    if ((size_t)slot >= kb->candidates.count) {
        if (!kb->candidates.learning_choices) {
            glide_learning_resolve(kb->learning, GLIDE_LEARNING_TOP_COMMITTED,
                                   0);
        }
        kbd_reset_candidates(kb);
        kbd_redraw_candidates(kb);
        return KbdCandidateDismissed;
    }
    kb->candidates.owner = owner;
    kb->candidates.touch_id = touch_id;
    kb->candidates.pointer_button = pointer_button;
    kb->candidates.pressed_slot = (size_t)slot;
    kb->candidates.pressed_inside = true;
    kbd_redraw_candidates(kb);
    return KbdCandidateClaimed;
}

static bool
kbd_candidate_is_owner(const struct kbd *kb, enum kbd_candidate_owner owner,
                       int32_t touch_id, uint32_t pointer_button)
{
    return kb->candidates.owner == owner &&
           (owner != KbdCandidateOwnerTouch ||
            kb->candidates.touch_id == touch_id) &&
           (owner != KbdCandidateOwnerPointer ||
            kb->candidates.pointer_button == pointer_button);
}

static enum kbd_candidate_event
kbd_candidate_move(struct kbd *kb, enum kbd_candidate_owner owner,
                   int32_t touch_id, int32_t x, int32_t y)
{
    bool inside;

    if (!kb->candidates.count ||
        kb->candidates.owner == KbdCandidateOwnerNone) {
        return KbdCandidateMiss;
    }
    if (!kbd_candidate_is_owner(kb, owner, touch_id,
                                kb->candidates.pointer_button)) {
        return KbdCandidateOwned;
    }
    inside = kbd_candidate_slot(kb, x, y) == (int)kb->candidates.pressed_slot;
    if (inside != kb->candidates.pressed_inside) {
        kb->candidates.pressed_inside = inside;
        kbd_redraw_candidates(kb);
    }
    return KbdCandidateOwned;
}

static enum kbd_candidate_event
kbd_candidate_release(struct kbd *kb, enum kbd_candidate_owner owner,
                      int32_t touch_id, uint32_t pointer_button, int32_t x,
                      int32_t y, bool use_coordinates, uint32_t time)
{
    struct glide_match selected;
    uint32_t codes[GLIDE_MAX_WORD];
    uint8_t case_mods;
    uint8_t undo_count;
    size_t selected_slot;
    bool commit;
    bool learning_choices;

    if (!kb->candidates.count ||
        kb->candidates.owner == KbdCandidateOwnerNone) {
        return KbdCandidateMiss;
    }
    if (!kbd_candidate_is_owner(kb, owner, touch_id, pointer_button)) {
        return KbdCandidateOwned;
    }
    commit = use_coordinates ? kbd_candidate_slot(kb, x, y) ==
                                   (int)kb->candidates.pressed_slot
                             : kb->candidates.pressed_inside;
    selected_slot = kb->candidates.pressed_slot;
    case_mods = kb->candidates.case_mods;
    undo_count = kb->glide_undo_count;
    learning_choices = kb->candidates.learning_choices;
    if (!learning_choices)
        selected = kb->candidates.matches[selected_slot];
    kbd_reset_candidates(kb);
    if (learning_choices) {
        if (commit) {
            glide_learning_resolve(
                kb->learning,
                selected_slot == 0 ? GLIDE_LEARNING_EXPLICIT_USER_MISSWIPE
                                   : GLIDE_LEARNING_EXPLICIT_LOOKUP_FAILURE,
                0);
        } else {
            glide_learning_clear(kb->learning);
        }
        kbd_redraw_candidates(kb);
        return KbdCandidateOwned;
    }
    if (commit && kbd_ascii_word_codes(selected.word, selected.length, codes)) {
        kbd_clear_glide_undo(kb);
        kbd_emit_backspaces(kb, undo_count, time, false);
        kbd_emit_ascii_word_case(kb, selected.word, selected.length, codes,
                                 time, case_mods, false);
        glide_learning_resolve(
            kb->learning,
            selected_slot ? GLIDE_LEARNING_ALTERNATE_SELECTED
                          : GLIDE_LEARNING_TOP_COMMITTED,
            selected_slot + 1);
    }
    kbd_redraw_candidates(kb);
    return KbdCandidateOwned;
}

enum kbd_candidate_event
kbd_candidate_touch_down(struct kbd *kb, int32_t id, int32_t x, int32_t y)
{
    return kbd_candidate_begin(kb, KbdCandidateOwnerTouch, id, 0, x, y);
}

enum kbd_candidate_event
kbd_candidate_touch_motion(struct kbd *kb, int32_t id, int32_t x, int32_t y)
{
    return kbd_candidate_move(kb, KbdCandidateOwnerTouch, id, x, y);
}

enum kbd_candidate_event
kbd_candidate_touch_up(struct kbd *kb, int32_t id, uint32_t time)
{
    return kbd_candidate_release(kb, KbdCandidateOwnerTouch, id, 0, 0, 0, false,
                                 time);
}

enum kbd_candidate_event
kbd_candidate_pointer_button(struct kbd *kb, uint32_t button, bool pressed,
                             int32_t x, int32_t y, uint32_t time)
{
    if (pressed) {
        return kbd_candidate_begin(kb, KbdCandidateOwnerPointer, 0, button, x,
                                   y);
    }
    return kbd_candidate_release(kb, KbdCandidateOwnerPointer, 0, button, x, y,
                                 true, time);
}

enum kbd_candidate_event
kbd_candidate_pointer_motion(struct kbd *kb, int32_t x, int32_t y)
{
    return kbd_candidate_move(kb, KbdCandidateOwnerPointer, 0, x, y);
}

static bool
kbd_is_word_punctuation(const struct kbd *kb, const struct key *key)
{
    bool shifted;

    if (key->code_mod != NoMod && key->code_mod != Shift) {
        return false;
    }
    shifted = kbd_effective_modifiers(kb, key) & Shift;
    return (shifted && key->code >= KEY_1 && key->code <= KEY_0) ||
           (key->code >= KEY_MINUS && key->code <= KEY_EQUAL) ||
           (key->code >= KEY_LEFTBRACE && key->code <= KEY_RIGHTBRACE) ||
           (key->code >= KEY_SEMICOLON && key->code <= KEY_GRAVE) ||
           key->code == KEY_BACKSLASH ||
           (key->code >= KEY_COMMA && key->code <= KEY_SLASH);
}

bool
kbd_begin_glide_followup(struct kbd *kb, const struct key *key, uint32_t time)
{
    uint8_t count = kb->glide_undo_count;

    if (!key || kb->compose ||
        (kb->mods & (Ctrl | Alt | Super | AltGr))) {
        kbd_clear_glide_undo(kb);
        return false;
    }
    if (key->type == Mod && (key->code == Shift || key->code == CapsLock)) {
        return false;
    }
    if (key->type == Code)
        glide_learning_resolve_correction(kb->learning, false);
    if (!count || count > GLIDE_MAX_WORD + 1) {
        kbd_clear_glide_undo(kb);
        return false;
    }
    if (key->type != Code) {
        glide_learning_resolve(kb->learning, GLIDE_LEARNING_TOP_COMMITTED, 0);
        kbd_clear_glide_undo(kb);
        return false;
    }

    kbd_clear_glide_undo(kb);
    if (key->code == KEY_BACKSPACE && key->code_mod == NoMod &&
        !(kb->mods & Shift)) {
        kbd_emit_backspaces(kb, count, time, true);
        glide_learning_mark_retracted(kb->learning);
        return true;
    }
    if (key->code == KEY_SPACE && key->code_mod == NoMod &&
        !(kb->mods & Shift)) {
        glide_learning_resolve(kb->learning, GLIDE_LEARNING_TOP_COMMITTED, 0);
        return true;
    }
    if (kbd_is_word_punctuation(kb, key)) {
        kbd_emit_backspaces(kb, 1, time, true);
    }
    glide_learning_resolve(kb->learning, GLIDE_LEARNING_TOP_COMMITTED, 0);
    return false;
}

static void
kbd_draw_candidates(struct kbd *kb)
{
    struct clr_scheme *scheme = &kb->schemes[0];
    uint32_t height = kb->layout->keyheight;

    if (!kb->candidates.count || !height) {
        return;
    }
    size_t slots = kb->candidates.learning_choices ? 2 : GLIDE_MAX_MATCHES;
    for (size_t i = 0; i < slots; i++) {
        uint32_t x = kbd_candidate_boundary(kb, i);
        uint32_t end = kbd_candidate_boundary(kb, i + 1);
        uint32_t width = end - x;
        bool highlighted =
            i == 0 ||
            (kb->candidates.owner != KbdCandidateOwnerNone &&
             kb->candidates.pressed_slot == i && kb->candidates.pressed_inside);
        Color color = highlighted ? scheme->high : scheme->fg;

        draw_inset(kb->surf, x, 0, width, height, KBD_KEY_BORDER, color,
                   scheme->rounding);
        if (kb->candidates.learning_choices && i < 2) {
            const char *label = i ? "Missing" : "Misswipe";
            drw_draw_text_bounded(kb->surf, scheme->text, x, 0, width, height,
                                  KBD_KEY_BORDER, label, (int)strlen(label),
                                  scheme->font_description);
        } else if (i < kb->candidates.count) {
            char label[GLIDE_MAX_WORD];
            const struct glide_match *match = &kb->candidates.matches[i];

            for (size_t j = 0; j < match->length; j++) {
                label[j] = kbd_case_letter(match->word[j], j,
                                           kb->candidates.case_mods);
            }
            drw_draw_text_bounded(kb->surf, scheme->text, x, 0, width, height,
                                  KBD_KEY_BORDER, label, (int)match->length,
                                  scheme->font_description);
        }
    }
}

void
kbd_draw_layout(struct kbd *kb)
{
    struct drwsurf *d = kb->surf;
    struct key *next_key = kb->layout->keys;
    if (kb->debug)
        fprintf(stderr, "Draw layout\n");

    drw_fill_rectangle(d, kb->schemes[0].bg, 0, 0, kb->w, kb->h, 0);

    while (next_key->type != Last) {
        if ((next_key->type == Pad) || (next_key->type == EndRow)) {
            next_key++;
            continue;
        }
        if ((next_key->type == Mod && kb->mods & next_key->code) ||
            (next_key->type == Compose && kb->compose)) {
            kbd_draw_key(kb, next_key, Press);
        } else {
            kbd_draw_key(kb, next_key, None);
        }
        next_key++;
    }
    kbd_draw_candidates(kb);
    wl_surface_damage(d->surf, 0, 0, kb->w, kb->h);
}

void
kbd_resize(struct kbd *kb, struct layout *layouts, uint8_t layoutcount)
{
    fprintf(stderr, "Resize %dx%d %f, %d layouts\n", kb->w, kb->h, kb->scale,
            layoutcount);

    kbd_reset_candidates(kb);
    drwsurf_resize(kb->surf, kb->w, kb->h, kb->scale);
    drwsurf_resize(kb->popup_surf, kb->w, kb->h * 2, kb->scale);
    for (int i = 0; i < layoutcount; i++) {
        if (kb->debug) {
            if (layouts[i].name)
                fprintf(stderr, "Initialising layout %s, keymap %s\n",
                        layouts[i].name, layouts[i].keymap_name);
            else
                fprintf(stderr, "Initialising unnamed layout %d, keymap %s\n",
                        i, layouts[i].keymap_name);
        }
        kbd_init_layout(&layouts[i], kb->w, kb->h);
    }
    kbd_draw_layout(kb);
}

void
draw_inset(struct drwsurf *ds, uint32_t x, uint32_t y, uint32_t width,
           uint32_t height, uint32_t border, Color color, int rounding)
{
    drw_fill_rectangle(ds, color, x + border, y + border, width - (border * 2),
                       height - (border * 2), rounding);
}
void
draw_over_inset(struct drwsurf *ds, uint32_t x, uint32_t y, uint32_t width,
                uint32_t height, uint32_t border, Color color, int rounding)
{
    drw_over_rectangle(ds, color, x + border, y + border, width - (border * 2),
                       height - (border * 2), rounding);
}

void
create_and_upload_keymap(struct kbd *kb, const char *name, uint32_t comp_unichr,
                         uint32_t comp_shift_unichr)
{
    int keymap_index = -1;
    for (int i = 0; i < NUMKEYMAPS; i++) {
        if (!strcmp(keymap_names[i], name)) {
            keymap_index = i;
        }
    }
    if (keymap_index == -1) {
        fprintf(stderr, "No such keymap defined: %s\n", name);
        exit(9);
    }
    const char *keymap_template = keymaps[keymap_index];
    size_t keymap_size = strlen(keymap_template) + 64;
    char *keymap_str = malloc(keymap_size);
    sprintf(keymap_str, keymap_template, comp_unichr, comp_shift_unichr);
    keymap_size = strlen(keymap_str);
    int keymap_fd = os_create_anonymous_file(keymap_size);
    if (keymap_fd < 0) {
        die("could not create keymap fd\n");
    }
    void *ptr = mmap(NULL, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     keymap_fd, 0);
    if (ptr == (void *)-1) {
        die("could not map keymap data\n");
    }
    if (kb->vkbd == NULL) {
        die("kb.vkbd = NULL\n");
    }
    strcpy(ptr, keymap_str);
    zwp_virtual_keyboard_v1_keymap(kb->vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                   keymap_fd, keymap_size);
    free((void *)keymap_str);
}
