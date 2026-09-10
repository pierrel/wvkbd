#ifndef LETTERS_H
#define LETTERS_H

#include <stdbool.h>
#include <stdint.h>

bool glide_letter_from_evdev(uint32_t code, char *letter);
bool glide_letter_to_evdev(char letter, uint32_t *code);

#endif
