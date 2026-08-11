#ifndef SUZYCUBE_INPUT_EVDEV_H
#define SUZYCUBE_INPUT_EVDEV_H

#include <linux/input.h>
#include <stddef.h>
#include <stdint.h>

#define SC_EVDEV_WORD_BITS (8U * sizeof(unsigned long))
#define SC_EVDEV_KEY_WORDS \
    ((KEY_MAX + 1U + SC_EVDEV_WORD_BITS - 1U) / SC_EVDEV_WORD_BITS)

enum sc_evdev_face_mapping {
    SC_EVDEV_FACE_UNKNOWN = 0,
    SC_EVDEV_FACE_XBOX_POSITIONAL,
    SC_EVDEV_FACE_NINTENDO_LABELS,
};

int sc_evdev_test_bit(const unsigned long *bits, size_t words, int code);
int sc_evdev_sdl_button_index(const unsigned long *bits, size_t words,
                              int code);
int sc_evdev_code_for_sdl_button(const unsigned long *bits, size_t words,
                                 int button_index);
void sc_evdev_apply_button_snapshot(const unsigned long *state, size_t words,
                                    const int *logical_codes, size_t count,
                                    uint8_t *buttons);
enum sc_evdev_face_mapping sc_evdev_classify_face_mapping(
    const unsigned long *bits, size_t words,
    int bind_a, int bind_b, int bind_x, int bind_y);

#endif
