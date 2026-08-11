/* Pure evdev helpers shared by the runtime adapter and its host unit test. */
#include "input_evdev.h"

int sc_evdev_test_bit(const unsigned long *bits, size_t words, int code)
{
    if (!bits || code < 0 || code > KEY_MAX)
        return 0;
    size_t word = (size_t)code / SC_EVDEV_WORD_BITS;
    if (word >= words)
        return 0;
    return (int)((bits[word] >> ((size_t)code % SC_EVDEV_WORD_BITS)) & 1UL);
}

/* SDL's Linux joystick backend numbers EV_KEY buttons in two passes: gamepad
 * codes first, then every lower EV_KEY code. Mirroring that exact order
 * lets the adapter relate an SDL mapping (b0, b1, ...) to the kernel bitmap
 * without relying on a controller name or a firmware database. */
static int visit_button_codes(const unsigned long *bits, size_t words,
                              int wanted_code, int wanted_index,
                              int return_code)
{
    int index = 0;
    for (int code = BTN_JOYSTICK; code < KEY_MAX; code++) {
        if (!sc_evdev_test_bit(bits, words, code))
            continue;
        if ((wanted_code >= 0 && code == wanted_code) ||
            (wanted_index >= 0 && index == wanted_index))
            return return_code ? code : index;
        index++;
    }
    for (int code = 0; code < BTN_JOYSTICK; code++) {
        if (!sc_evdev_test_bit(bits, words, code))
            continue;
        if ((wanted_code >= 0 && code == wanted_code) ||
            (wanted_index >= 0 && index == wanted_index))
            return return_code ? code : index;
        index++;
    }
    return -1;
}

int sc_evdev_sdl_button_index(const unsigned long *bits, size_t words,
                              int code)
{
    if (!sc_evdev_test_bit(bits, words, code))
        return -1;
    return visit_button_codes(bits, words, code, -1, 0);
}

int sc_evdev_code_for_sdl_button(const unsigned long *bits, size_t words,
                                 int button_index)
{
    if (button_index < 0)
        return -1;
    return visit_button_codes(bits, words, -1, button_index, 1);
}

void sc_evdev_apply_button_snapshot(const unsigned long *state, size_t words,
                                    const int *logical_codes, size_t count,
                                    uint8_t *buttons)
{
    if (!logical_codes || !buttons)
        return;
    for (size_t i = 0; i < count; i++) {
        if (logical_codes[i] >= 0)
            buttons[i] = (uint8_t)sc_evdev_test_bit(
                state, words, logical_codes[i]);
    }
}

/* The gpio-keys pads of these handhelds all share the CRC-less generic SDL
 * GUID (bus 0x19, VID/PID 0001:0001), so a firmware mapping written for one
 * device can be applied to another whose key list differs -- every b<N>
 * ordinal then lands on the wrong physical button.  When that happens the
 * kernel keycodes themselves are the only trustworthy layout: BTN_SOUTH is
 * the south face button on every one of these drivers.  Returns 0 (and
 * writes nothing) unless all four face codes are declared. */
int sc_evdev_semantic_codes(const unsigned long *bits, size_t words,
                            int *logical_codes, size_t count)
{
    static const int semantic[SC_PAD_COUNT] = {
        [SC_PAD_A] = BTN_SOUTH, [SC_PAD_B] = BTN_EAST,
        [SC_PAD_X] = BTN_WEST, [SC_PAD_Y] = BTN_NORTH,
        [SC_PAD_BACK] = BTN_SELECT, [SC_PAD_GUIDE] = BTN_MODE,
        [SC_PAD_START] = BTN_START,
        [SC_PAD_LEFTSTICK] = BTN_THUMBL, [SC_PAD_RIGHTSTICK] = BTN_THUMBR,
        [SC_PAD_LEFTSHOULDER] = BTN_TL, [SC_PAD_RIGHTSHOULDER] = BTN_TR,
        [SC_PAD_DPAD_UP] = BTN_DPAD_UP, [SC_PAD_DPAD_DOWN] = BTN_DPAD_DOWN,
        [SC_PAD_DPAD_LEFT] = BTN_DPAD_LEFT,
        [SC_PAD_DPAD_RIGHT] = BTN_DPAD_RIGHT,
    };
    if (!logical_codes ||
        !sc_evdev_test_bit(bits, words, BTN_SOUTH) ||
        !sc_evdev_test_bit(bits, words, BTN_EAST) ||
        !sc_evdev_test_bit(bits, words, BTN_NORTH) ||
        !sc_evdev_test_bit(bits, words, BTN_WEST))
        return 0;
    for (size_t i = 0; i < count; i++) {
        const int code = i < SC_PAD_COUNT ? semantic[i] : 0;
        logical_codes[i] = code && sc_evdev_test_bit(bits, words, code)
                         ? code : -1;
    }
    return 1;
}

enum sc_evdev_face_mapping sc_evdev_classify_face_mapping(
    const unsigned long *bits, size_t words,
    int bind_a, int bind_b, int bind_x, int bind_y)
{
    const int south = sc_evdev_sdl_button_index(bits, words, BTN_SOUTH);
    const int east = sc_evdev_sdl_button_index(bits, words, BTN_EAST);
    const int north = sc_evdev_sdl_button_index(bits, words, BTN_NORTH);
    const int west = sc_evdev_sdl_button_index(bits, words, BTN_WEST);

    if (south < 0 || east < 0 || north < 0 || west < 0)
        return SC_EVDEV_FACE_UNKNOWN;

    /* Xbox/InControl position semantics: A south, B east, X west, Y north. */
    if (bind_a == south && bind_b == east &&
        bind_x == west && bind_y == north)
        return SC_EVDEV_FACE_XBOX_POSITIONAL;

    /* Common R36S mapping follows the printed Nintendo-style letters instead:
     * A east, B south, X north, Y west.  The game's fixed Xbox glyph profile
     * consequently presents both pairs reversed unless we normalize it. */
    if (bind_a == east && bind_b == south &&
        bind_x == north && bind_y == west)
        return SC_EVDEV_FACE_NINTENDO_LABELS;

    return SC_EVDEV_FACE_UNKNOWN;
}
