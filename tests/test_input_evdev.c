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

    /* Foreign-mapping recovery: an muOS-Keys-style pad (same generic GUID as
     * the R36 Deeplay-keys, different key list) must map by keycode
     * semantics, ignoring the foreign mapping's ordinals entirely. */
    unsigned long muos[SC_EVDEV_KEY_WORDS];
    memset(muos, 0, sizeof muos);
    const int muos_codes[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
        BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR,
        KEY_VOLUMEDOWN, KEY_VOLUMEUP,
    };
    for (size_t i = 0; i < sizeof muos_codes / sizeof *muos_codes; i++)
        set_bit(muos, muos_codes[i]);
    int semantic[SC_PAD_COUNT + 1];
    memset(semantic, 0x7f, sizeof semantic);
    assert(sc_evdev_semantic_codes(muos, SC_EVDEV_KEY_WORDS,
                                   semantic, SC_PAD_COUNT + 1) == 1);
    assert(semantic[SC_PAD_A] == BTN_SOUTH);
    assert(semantic[SC_PAD_B] == BTN_EAST);
    assert(semantic[SC_PAD_X] == BTN_WEST);
    assert(semantic[SC_PAD_Y] == BTN_NORTH);
    assert(semantic[SC_PAD_BACK] == BTN_SELECT);
    assert(semantic[SC_PAD_START] == BTN_START);
    assert(semantic[SC_PAD_GUIDE] == BTN_MODE);
    assert(semantic[SC_PAD_LEFTSHOULDER] == BTN_TL);
    assert(semantic[SC_PAD_RIGHTSHOULDER] == BTN_TR);
    assert(semantic[SC_PAD_LEFTSTICK] == BTN_THUMBL);
    assert(semantic[SC_PAD_RIGHTSTICK] == BTN_THUMBR);
    assert(semantic[SC_PAD_DPAD_UP] == -1);   /* dpad ausente fica com SDL */
    assert(semantic[SC_PAD_COUNT] == -1);     /* alem dos 15: sem semantica */

    /* Sem os quatro botoes de face nao ha layout confiavel: nada muda. */
    unsigned long faceless[SC_EVDEV_KEY_WORDS];
    memset(faceless, 0, sizeof faceless);
    set_bit(faceless, BTN_SOUTH);
    set_bit(faceless, BTN_EAST);
    int untouched[SC_PAD_COUNT];
    memset(untouched, 0x55, sizeof untouched);
    int untouched_copy[SC_PAD_COUNT];
    memcpy(untouched_copy, untouched, sizeof untouched);
    assert(sc_evdev_semantic_codes(faceless, SC_EVDEV_KEY_WORDS,
                                   untouched, SC_PAD_COUNT) == 0);
    assert(memcmp(untouched, untouched_copy, sizeof untouched) == 0);

    assert(sc_evdev_test_bit(bits, SC_EVDEV_KEY_WORDS, -1) == 0);
    assert(sc_evdev_test_bit(bits, SC_EVDEV_KEY_WORDS, KEY_MAX + 1) == 0);

    /* Eixo analogico de verdade tem range largo; dpad-como-ABS nao. */
    assert(sc_evdev_axis_is_analog(0, 255) == 1);
    assert(sc_evdev_axis_is_analog(-2048, 2047) == 1);
    assert(sc_evdev_axis_is_analog(-32768, 32767) == 1);
    assert(sc_evdev_axis_is_analog(-1, 1) == 0);   /* dpad declarado ABS */
    assert(sc_evdev_axis_is_analog(0, 1) == 0);    /* eixo degenerado */
    assert(sc_evdev_axis_is_analog(0, 2) == 0);
    assert(sc_evdev_axis_is_analog(0, 0) == 0);
    assert(sc_evdev_axis_is_analog(10, 0) == 0);   /* range invertido */

    /* Normalizacao pelo min/max reais: extremos, centro e clamp. */
    assert(sc_evdev_axis_normalize(0, 0, 255) == -32767);
    assert(sc_evdev_axis_normalize(255, 0, 255) == 32767);
    int center = sc_evdev_axis_normalize(128, 0, 255);
    assert(center > -300 && center < 300);
    assert(sc_evdev_axis_normalize(0, -32768, 32767) == 0);
    assert(sc_evdev_axis_normalize(-32768, -32768, 32767) == -32767);
    assert(sc_evdev_axis_normalize(32767, -32768, 32767) == 32767);
    assert(sc_evdev_axis_normalize(-999, 0, 255) == -32767);   /* clamp */
    assert(sc_evdev_axis_normalize(999, 0, 255) == 32767);     /* clamp */
    assert(sc_evdev_axis_normalize(5, 7, 7) == 0);  /* range vazio: neutro */

    puts("SUZY CUBE EVDEV UNIT: PASS");
    return 0;
}
