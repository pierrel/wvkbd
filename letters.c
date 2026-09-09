#include <linux/input-event-codes.h>
#include <stddef.h>

#include "letters.h"

struct glide_letter_code {
    char letter;
    uint32_t code;
};

/* Linux letter keycodes are noncontiguous: this is the sole conversion table. */
static const struct glide_letter_code glide_letters[] = {
    {'a', KEY_A}, {'b', KEY_B}, {'c', KEY_C}, {'d', KEY_D}, {'e', KEY_E},
    {'f', KEY_F}, {'g', KEY_G}, {'h', KEY_H}, {'i', KEY_I}, {'j', KEY_J},
    {'k', KEY_K}, {'l', KEY_L}, {'m', KEY_M}, {'n', KEY_N}, {'o', KEY_O},
    {'p', KEY_P}, {'q', KEY_Q}, {'r', KEY_R}, {'s', KEY_S}, {'t', KEY_T},
    {'u', KEY_U}, {'v', KEY_V}, {'w', KEY_W}, {'x', KEY_X}, {'y', KEY_Y},
    {'z', KEY_Z},
};

bool
glide_letter_from_evdev(uint32_t code, char *letter)
{
    for (size_t i = 0; i < sizeof(glide_letters) / sizeof(glide_letters[0]); i++) {
        if (glide_letters[i].code == code) {
            if (letter) *letter = glide_letters[i].letter;
            return true;
        }
    }
    return false;
}

bool
glide_letter_to_evdev(char letter, uint32_t *code)
{
    for (size_t i = 0; i < sizeof(glide_letters) / sizeof(glide_letters[0]); i++) {
        if (glide_letters[i].letter == letter) {
            if (code) *code = glide_letters[i].code;
            return true;
        }
    }
    return false;
}
