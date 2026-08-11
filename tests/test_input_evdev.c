#include "input_evdev.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_bit(unsigned long *bits, int code)
{
    bits[(size_t)code / SC_EVDEV_WORD_BITS] |=
        1UL << ((size_t)code % SC_EVDEV_WORD_BITS);
}

int main(void)
{
    unsigned long bits[SC_EVDEV_KEY_WORDS];
    memset(bits, 0, sizeof bits);

    const int codes[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
        BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2,
    };
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++)
        set_bit(bits, codes[i]);
    /* Lower EV_KEY codes are numbered in SDL's second pass, in code order. */
    set_bit(bits, KEY_ENTER);
    set_bit(bits, BTN_0);

    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     BTN_SOUTH) == 0);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     BTN_EAST) == 1);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     BTN_NORTH) == 2);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     BTN_WEST) == 3);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     BTN_TRIGGER_HAPPY2) == 11);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS,
                                     KEY_ENTER) == 12);
    assert(sc_evdev_sdl_button_index(bits, SC_EVDEV_KEY_WORDS, BTN_0) == 13);

    assert(sc_evdev_code_for_sdl_button(bits, SC_EVDEV_KEY_WORDS, 0) ==
           BTN_SOUTH);
    assert(sc_evdev_code_for_sdl_button(bits, SC_EVDEV_KEY_WORDS, 11) ==
           BTN_TRIGGER_HAPPY2);
    assert(sc_evdev_code_for_sdl_button(bits, SC_EVDEV_KEY_WORDS, 12) ==
           KEY_ENTER);
    assert(sc_evdev_code_for_sdl_button(bits, SC_EVDEV_KEY_WORDS, 13) ==
           BTN_0);
    assert(sc_evdev_code_for_sdl_button(bits, SC_EVDEV_KEY_WORDS, 14) == -1);

    assert(sc_evdev_classify_face_mapping(
               bits, SC_EVDEV_KEY_WORDS, 0, 1, 3, 2) ==
           SC_EVDEV_FACE_XBOX_POSITIONAL);
    assert(sc_evdev_classify_face_mapping(
               bits, SC_EVDEV_KEY_WORDS, 1, 0, 2, 3) ==
           SC_EVDEV_FACE_NINTENDO_LABELS);
    assert(sc_evdev_classify_face_mapping(
               bits, SC_EVDEV_KEY_WORDS, 0, 1, 2, 3) ==
           SC_EVDEV_FACE_UNKNOWN);

    /* A stale SDL DPAD_DOWN state is cleared by a neutral kernel snapshot,
     * then asserted again only while the kernel says the key is held. */
    unsigned long state[SC_EVDEV_KEY_WORDS];
    int logical_codes[] = { BTN_DPAD_DOWN, BTN_SOUTH, -1 };
    uint8_t logical_buttons[] = { 1, 1, 1 };
    memset(state, 0, sizeof state);
    sc_evdev_apply_button_snapshot(state, SC_EVDEV_KEY_WORDS,
                                   logical_codes, 3, logical_buttons);
    assert(logical_buttons[0] == 0);
    assert(logical_buttons[1] == 0);
    assert(logical_buttons[2] == 1); /* Unmapped controls stay with SDL. */
    set_bit(state, BTN_DPAD_DOWN);
    sc_evdev_apply_button_snapshot(state, SC_EVDEV_KEY_WORDS,
                                   logical_codes, 3, logical_buttons);
    assert(logical_buttons[0] == 1);

    assert(sc_evdev_test_bit(bits, SC_EVDEV_KEY_WORDS, -1) == 0);
    assert(sc_evdev_test_bit(bits, SC_EVDEV_KEY_WORDS, KEY_MAX + 1) == 0);
    puts("SUZY CUBE EVDEV UNIT: PASS");
    return 0;
}
