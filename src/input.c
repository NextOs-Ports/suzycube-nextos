/*
 * input.c -- ponte controle Linux -> entrada Android do Suzy Cube.
 *
 * O Suzy Cube ja vem com InControl DE FABRICA (perfis Linux e Android no
 * metadata) e com os eixos Horizontal/Vertical/Jump/Submit/Cancel.  O caminho
 * nativo do jogo, portanto, e' o mesmo do Android: o UnityPlayer recebe
 * KeyEvent/MotionEvent, a Unity publica o joystick, e o InControl enumera o
 * device e casa um perfil.  Nada de cursor, nada de toque sintetico, nada de
 * capturar botao: escrito do zero para este jogo.
 *
 * O que fazemos:
 *   - SDL normaliza o pad fisico (com fallback de joystick cru para pads fora
 *     da base do SDL);
 *   - o pad e' anunciado ao lado Java como um InputDevice de verdade, com nome
 *     que o InControl reconhece;
 *   - cada mudanca de botao vira KeyEvent ACTION_DOWN/UP com o KEYCODE Android
 *     correspondente, e os eixos viram um MotionEvent de SOURCE_JOYSTICK;
 *   - SELECT+START pede a saida pelo ciclo de vida nativo (pause + save).
 */

#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include "framework_bridge.h"
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "sc.h"
#include "input_evdev.h"
#include "input_policy.h"
#include "nx_elf.h"
#include "nxinput_core.h"

static SDL_GameController *controller;
static SDL_Joystick *raw_joystick;
static SDL_JoystickID controller_instance = -1;
static uint8_t buttons[SDL_CONTROLLER_BUTTON_MAX];
static uint8_t previous[SDL_CONTROLLER_BUTTON_MAX];
static volatile sig_atomic_t exit_requested;
static int input_diag;
static int input_active = 1;
static int screen_width = 1280;
static int screen_height = 720;
static nxinput_stick_filter stick_filter[2];
static int motion_non_neutral;
static int motion_neutral_pending;

void sc_input_request_exit(void) { exit_requested = 1; }
int sc_input_exit_requested(void) { return exit_requested; }

void sc_input_set_screen_size(int width, int height)
{
    if (width > 0)
        screen_width = width;
    if (height > 0)
        screen_height = height;
}

/* KEYCODEs do Android que o InControl le no perfil de gamepad. */
static const int android_key[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = 96,               /* KEYCODE_BUTTON_A */
    [SDL_CONTROLLER_BUTTON_B] = 97,               /* KEYCODE_BUTTON_B */
    [SDL_CONTROLLER_BUTTON_X] = 99,               /* KEYCODE_BUTTON_X */
    [SDL_CONTROLLER_BUTTON_Y] = 100,              /* KEYCODE_BUTTON_Y */
    [SDL_CONTROLLER_BUTTON_BACK] = 109,           /* KEYCODE_BUTTON_SELECT */
    [SDL_CONTROLLER_BUTTON_GUIDE] = 110,          /* KEYCODE_BUTTON_MODE */
    [SDL_CONTROLLER_BUTTON_START] = 108,          /* KEYCODE_BUTTON_START */
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = 106,      /* KEYCODE_BUTTON_THUMBL */
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = 107,     /* KEYCODE_BUTTON_THUMBR */
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = 102,   /* KEYCODE_BUTTON_L1 */
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 103,  /* KEYCODE_BUTTON_R1 */
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = 19,         /* KEYCODE_DPAD_UP */
    [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = 20,
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 21,
    [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = 22,
};

/* Gatilhos analogicos tambem viram botao (KEYCODE_BUTTON_L2/R2), porque em
 * boa parte dos handhelds L2/R2 sao digitais e o jogo perde dois botoes se so
 * existirem como eixo. */
#define KEY_BUTTON_L2 104
#define KEY_BUTTON_R2 105
static uint8_t trigger_down[2];
static uint8_t trigger_prev[2];

/* ------------------------------------------------------------- evdev probe */
/* SDL traduz bem o layout, mas alguns CFWs RK3326 perdem uma transicao de
 * release depois de muitos minutos. EVIOCGKEY e' um snapshot do estado atual,
 * portanto ele se autocorrige no quadro seguinte mesmo que a fila SDL tenha
 * perdido um evento. So usamos o no evdev quando nome + VID/PID identificam um
 * unico gamepad; em caso de ambiguidade, o caminho SDL original permanece. */
enum face_policy {
    FACE_POLICY_AUTO = 0,
    FACE_POLICY_FIRMWARE,
    FACE_POLICY_XBOX,
};

static enum face_policy configured_face_policy = FACE_POLICY_AUTO;

static struct {
    int fd;
    char path[64];
    unsigned long capabilities[SC_EVDEV_KEY_WORDS];
    int logical_code[SDL_CONTROLLER_BUTTON_MAX];
    int face_positional;
    int trigger_happy_direct;
    int semantic;
    int trigger_code[2];
    uint8_t trigger_state[2];
} evdev_pad = { .fd = -1, .trigger_code = { -1, -1 } };

/* input_evdev.h mirrors these SDL values to stay SDL-free. */
_Static_assert((int)SC_PAD_A == (int)SDL_CONTROLLER_BUTTON_A &&
               (int)SC_PAD_B == (int)SDL_CONTROLLER_BUTTON_B &&
               (int)SC_PAD_X == (int)SDL_CONTROLLER_BUTTON_X &&
               (int)SC_PAD_Y == (int)SDL_CONTROLLER_BUTTON_Y &&
               (int)SC_PAD_BACK == (int)SDL_CONTROLLER_BUTTON_BACK &&
               (int)SC_PAD_GUIDE == (int)SDL_CONTROLLER_BUTTON_GUIDE &&
               (int)SC_PAD_START == (int)SDL_CONTROLLER_BUTTON_START &&
               (int)SC_PAD_LEFTSTICK == (int)SDL_CONTROLLER_BUTTON_LEFTSTICK &&
               (int)SC_PAD_RIGHTSTICK ==
                   (int)SDL_CONTROLLER_BUTTON_RIGHTSTICK &&
               (int)SC_PAD_LEFTSHOULDER ==
                   (int)SDL_CONTROLLER_BUTTON_LEFTSHOULDER &&
               (int)SC_PAD_RIGHTSHOULDER ==
                   (int)SDL_CONTROLLER_BUTTON_RIGHTSHOULDER &&
               (int)SC_PAD_DPAD_UP == (int)SDL_CONTROLLER_BUTTON_DPAD_UP &&
               (int)SC_PAD_DPAD_DOWN == (int)SDL_CONTROLLER_BUTTON_DPAD_DOWN &&
               (int)SC_PAD_DPAD_LEFT == (int)SDL_CONTROLLER_BUTTON_DPAD_LEFT &&
               (int)SC_PAD_DPAD_RIGHT ==
                   (int)SDL_CONTROLLER_BUTTON_DPAD_RIGHT &&
               (int)SC_PAD_COUNT <= (int)SDL_CONTROLLER_BUTTON_MAX,
               "SC_PAD_* must mirror SDL_CONTROLLER_BUTTON_*");

/* Ordem posicional dos pads USB comuns quando nao ha mapeamento SDL. */
static const int raw_to_pad[] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    -1, -1,   /* 6/7 = L2/R2 digitais, tratados como gatilho */
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_GUIDE,
};

static void close_evdev_pad(void)
{
    if (evdev_pad.fd >= 0)
        close(evdev_pad.fd);
    evdev_pad.fd = -1;
    evdev_pad.path[0] = '\0';
    memset(evdev_pad.capabilities, 0, sizeof evdev_pad.capabilities);
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        evdev_pad.logical_code[i] = -1;
    evdev_pad.face_positional = 0;
    evdev_pad.trigger_happy_direct = 0;
    evdev_pad.semantic = 0;
    evdev_pad.trigger_code[0] = evdev_pad.trigger_code[1] = -1;
    evdev_pad.trigger_state[0] = evdev_pad.trigger_state[1] = 0;
}

/* Os pads gpio-keys destes handhelds compartilham o GUID generico sem CRC
 * (VID/PID 0001:0001): um mapping SDL escrito para OUTRO aparelho (nome do
 * mapping != nome do joystick) casa por GUID e desloca todos os ordinais --
 * foi assim que no RG35XXSP/muOS o mapping "Deeplay-keys" do R36 fez L2+R2
 * virarem SELECT+START e sairem do jogo.  Pads USB/BT reais tem VID/PID
 * proprios e nunca entram aqui. */
static int sdl_mapping_is_foreign(void)
{
    if (!controller)
        return 0;
    SDL_Joystick *joy = SDL_GameControllerGetJoystick(controller);
    if (!joy)
        return 0;
    if (SDL_JoystickGetVendor(joy) != 0x0001 ||
        SDL_JoystickGetProduct(joy) != 0x0001)
        return 0;
    const char *mapping_name = SDL_GameControllerName(controller);
    const char *device_name = SDL_JoystickName(joy);
    return mapping_name && device_name &&
           strcmp(mapping_name, device_name) != 0;
}

static const char *face_policy_name(void)
{
    return configured_face_policy == FACE_POLICY_FIRMWARE ? "firmware" :
           configured_face_policy == FACE_POLICY_XBOX ? "xbox" : "auto";
}

static int controller_button_ordinal(SDL_GameControllerButton button)
{
    SDL_GameControllerButtonBind bind =
        SDL_GameControllerGetBindForButton(controller, button);
    return bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON
         ? bind.value.button : -1;
}

static int has_positional_face_codes(const unsigned long *capabilities)
{
    return sc_evdev_test_bit(capabilities, SC_EVDEV_KEY_WORDS, BTN_SOUTH) &&
           sc_evdev_test_bit(capabilities, SC_EVDEV_KEY_WORDS, BTN_EAST) &&
           sc_evdev_test_bit(capabilities, SC_EVDEV_KEY_WORDS, BTN_NORTH) &&
           sc_evdev_test_bit(capabilities, SC_EVDEV_KEY_WORDS, BTN_WEST);
}

static void configure_evdev_mapping(void)
{
    if (controller && sdl_mapping_is_foreign() &&
        sc_evdev_semantic_codes(evdev_pad.capabilities, SC_EVDEV_KEY_WORDS,
                                evdev_pad.logical_code,
                                SDL_CONTROLLER_BUTTON_MAX)) {
        evdev_pad.semantic = 1;
        evdev_pad.trigger_code[0] = sc_evdev_test_bit(
            evdev_pad.capabilities, SC_EVDEV_KEY_WORDS, BTN_TL2)
            ? BTN_TL2 : -1;
        evdev_pad.trigger_code[1] = sc_evdev_test_bit(
            evdev_pad.capabilities, SC_EVDEV_KEY_WORDS, BTN_TR2)
            ? BTN_TR2 : -1;
        fprintf(stderr,
                "[sc/input] mapping SDL '%s' e' de outro aparelho (GUID "
                "generico); pad '%s' mapeado pelos keycodes do proprio "
                "evdev\n",
                SDL_GameControllerName(controller),
                SDL_JoystickName(SDL_GameControllerGetJoystick(controller)));
        fprintf(stderr,
                "[sc/input] snapshot autoritativo %s; "
                "face=keycodes-semanticos (politica=%s)\n",
                evdev_pad.path, face_policy_name());
    } else if (controller) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
            const int ordinal = controller_button_ordinal(
                (SDL_GameControllerButton)i);
            evdev_pad.logical_code[i] = sc_evdev_code_for_sdl_button(
                evdev_pad.capabilities, SC_EVDEV_KEY_WORDS, ordinal);
        }

        const int bind_a = controller_button_ordinal(SDL_CONTROLLER_BUTTON_A);
        const int bind_b = controller_button_ordinal(SDL_CONTROLLER_BUTTON_B);
        const int bind_x = controller_button_ordinal(SDL_CONTROLLER_BUTTON_X);
        const int bind_y = controller_button_ordinal(SDL_CONTROLLER_BUTTON_Y);
        const enum sc_evdev_face_mapping face =
            sc_evdev_classify_face_mapping(
                evdev_pad.capabilities, SC_EVDEV_KEY_WORDS,
                bind_a, bind_b, bind_x, bind_y);

        if (configured_face_policy == FACE_POLICY_XBOX)
            evdev_pad.face_positional =
                has_positional_face_codes(evdev_pad.capabilities);
        else if (configured_face_policy == FACE_POLICY_AUTO)
            evdev_pad.face_positional =
                face == SC_EVDEV_FACE_NINTENDO_LABELS;

        const char *effective = evdev_pad.face_positional
                              ? "xbox-posicional"
                              : face == SC_EVDEV_FACE_XBOX_POSITIONAL
                              ? "xbox-posicional-do-firmware"
                              : "mapeamento-do-firmware";
        fprintf(stderr,
                "[sc/input] snapshot autoritativo %s; face=%s (politica=%s)\n",
                evdev_pad.path, effective, face_policy_name());
    } else {
        for (int ordinal = 0;
             ordinal < (int)(sizeof raw_to_pad / sizeof *raw_to_pad);
             ordinal++) {
            const int logical = raw_to_pad[ordinal];
            if (logical >= 0)
                evdev_pad.logical_code[logical] =
                    sc_evdev_code_for_sdl_button(
                        evdev_pad.capabilities, SC_EVDEV_KEY_WORDS, ordinal);
        }
        fprintf(stderr, "[sc/input] snapshot autoritativo %s; pad cru\n",
                evdev_pad.path);
    }

    evdev_pad.trigger_happy_direct =
        !sc_evdev_test_bit(evdev_pad.capabilities, SC_EVDEV_KEY_WORDS,
                           BTN_SELECT) &&
        !sc_evdev_test_bit(evdev_pad.capabilities, SC_EVDEV_KEY_WORDS,
                           BTN_START) &&
        sc_evdev_test_bit(evdev_pad.capabilities, SC_EVDEV_KEY_WORDS,
                          BTN_TRIGGER_HAPPY1);
    if (evdev_pad.trigger_happy_direct)
        fprintf(stderr,
                "[sc/input] %s: TRIGGER_HAPPY1/2 -> SELECT/START direto\n",
                evdev_pad.path);
}

static int open_evdev_pad(const char *physical, int vendor, int product)
{
    close_evdev_pad();
    if (!physical || !*physical || vendor <= 0 || product <= 0)
        return 0;

    int matched_fd = -1;
    int matches = 0;
    char matched_path[64] = {0};
    unsigned long matched_capabilities[SC_EVDEV_KEY_WORDS];
    memset(matched_capabilities, 0, sizeof matched_capabilities);

    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;

        struct input_id id;
        char name[256];
        unsigned long capabilities[SC_EVDEV_KEY_WORDS];
        memset(&id, 0, sizeof id);
        memset(name, 0, sizeof name);
        memset(capabilities, 0, sizeof capabilities);
        const int usable =
            ioctl(fd, EVIOCGID, &id) >= 0 &&
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof capabilities),
                  capabilities) >= 0 &&
            id.vendor == (unsigned int)vendor &&
            id.product == (unsigned int)product &&
            strcmp(name, physical) == 0 &&
            sc_evdev_test_bit(capabilities, SC_EVDEV_KEY_WORDS, BTN_SOUTH);
        if (!usable) {
            close(fd);
            continue;
        }

        matches++;
        if (matched_fd < 0) {
            matched_fd = fd;
            snprintf(matched_path, sizeof matched_path, "%s", path);
            memcpy(matched_capabilities, capabilities,
                   sizeof matched_capabilities);
        } else {
            close(fd);
        }
    }

    if (matches != 1) {
        if (matched_fd >= 0)
            close(matched_fd);
        if (matches > 1)
            fprintf(stderr,
                    "[sc/input] evdev ambiguo para %s (%04x:%04x); "
                    "mantendo SDL\n",
                    physical, vendor & 0xffff, product & 0xffff);
        return 0;
    }

    evdev_pad.fd = matched_fd;
    snprintf(evdev_pad.path, sizeof evdev_pad.path, "%s", matched_path);
    memcpy(evdev_pad.capabilities, matched_capabilities,
           sizeof evdev_pad.capabilities);
    configure_evdev_mapping();
    return 1;
}

/* Compatibilidade quando o firmware nao permite associar um no evdev exato:
 * conserva o caminho antigo por ordinal, mas filtra nome e VID/PID. */
static int th_select_ordinal = -1;
static int th_start_ordinal = -1;

static void find_trigger_happy_ordinals(const char *physical,
                                        int vendor, int product)
{
    th_select_ordinal = th_start_ordinal = -1;
    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;
        struct input_id id;
        char name[256];
        unsigned long keyb[SC_EVDEV_KEY_WORDS];
        memset(&id, 0, sizeof id);
        memset(name, 0, sizeof name);
        memset(keyb, 0, sizeof keyb);
        if (ioctl(fd, EVIOCGID, &id) >= 0 &&
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keyb), keyb) >= 0 &&
            (!physical || !*physical || strcmp(name, physical) == 0) &&
            (vendor <= 0 || id.vendor == (unsigned int)vendor) &&
            (product <= 0 || id.product == (unsigned int)product) &&
            sc_evdev_test_bit(keyb, SC_EVDEV_KEY_WORDS, BTN_GAMEPAD) &&
            !sc_evdev_test_bit(keyb, SC_EVDEV_KEY_WORDS, BTN_SELECT) &&
            !sc_evdev_test_bit(keyb, SC_EVDEV_KEY_WORDS, BTN_START) &&
            sc_evdev_test_bit(keyb, SC_EVDEV_KEY_WORDS,
                              BTN_TRIGGER_HAPPY1)) {
            th_select_ordinal = sc_evdev_sdl_button_index(
                keyb, SC_EVDEV_KEY_WORDS, BTN_TRIGGER_HAPPY1);
            th_start_ordinal = sc_evdev_sdl_button_index(
                keyb, SC_EVDEV_KEY_WORDS, BTN_TRIGGER_HAPPY2);
            fprintf(stderr,
                    "[sc/input] fallback %s: TRIGGER_HAPPY1/2 "
                    "nos ordinais %d/%d\n",
                    path, th_select_ordinal, th_start_ordinal);
            close(fd);
            return;
        }
        close(fd);
    }
}

static void apply_trigger_happy_fallback(void)
{
    if (evdev_pad.fd >= 0)
        return;
    if (th_select_ordinal < 0 && th_start_ordinal < 0)
        return;
    SDL_Joystick *joy = controller ? SDL_GameControllerGetJoystick(controller)
                                   : raw_joystick;
    if (!joy)
        return;
    int count = SDL_JoystickNumButtons(joy);
    if (th_select_ordinal >= 0 && th_select_ordinal < count &&
        SDL_JoystickGetButton(joy, th_select_ordinal))
        buttons[SDL_CONTROLLER_BUTTON_BACK] = 1;
    if (th_start_ordinal >= 0 && th_start_ordinal < count &&
        SDL_JoystickGetButton(joy, th_start_ordinal))
        buttons[SDL_CONTROLLER_BUTTON_START] = 1;
}

static void apply_evdev_snapshot(void)
{
    if (evdev_pad.fd < 0)
        return;

    unsigned long state[SC_EVDEV_KEY_WORDS];
    memset(state, 0, sizeof state);
    if (ioctl(evdev_pad.fd, EVIOCGKEY(sizeof state), state) < 0) {
        char failed_path[sizeof evdev_pad.path];
        snprintf(failed_path, sizeof failed_path, "%s", evdev_pad.path);
        close_evdev_pad();
        fprintf(stderr,
                "[sc/input] snapshot %s falhou; retomando estado SDL\n",
                failed_path);
        return;
    }

    sc_evdev_apply_button_snapshot(
        state, SC_EVDEV_KEY_WORDS, evdev_pad.logical_code,
        SDL_CONTROLLER_BUTTON_MAX, buttons);

    for (int t = 0; t < 2; t++)
        evdev_pad.trigger_state[t] =
            evdev_pad.trigger_code[t] >= 0 &&
            sc_evdev_test_bit(state, SC_EVDEV_KEY_WORDS,
                              evdev_pad.trigger_code[t]);

    if (evdev_pad.trigger_happy_direct) {
        buttons[SDL_CONTROLLER_BUTTON_BACK] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_TRIGGER_HAPPY1);
        buttons[SDL_CONTROLLER_BUTTON_START] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_TRIGGER_HAPPY2);
    }

    if (evdev_pad.face_positional) {
        buttons[SDL_CONTROLLER_BUTTON_A] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_SOUTH);
        buttons[SDL_CONTROLLER_BUTTON_B] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_EAST);
        buttons[SDL_CONTROLLER_BUTTON_X] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_WEST);
        buttons[SDL_CONTROLLER_BUTTON_Y] = sc_evdev_test_bit(
            state, SC_EVDEV_KEY_WORDS, BTN_NORTH);
    }
}

/* ------------------------------------------------------------------ leitura */

static void reset_motion_filters(void)
{
    memset(stick_filter, 0, sizeof stick_filter);
}

static void request_neutral_motion(void)
{
    /* The Android input stack must see a neutral sample across every focus or
     * device boundary. Otherwise Unity/InControl can retain the last axes even
     * though SDL has already discarded the controller state. */
    motion_neutral_pending = 1;
    reset_motion_filters();
}

static Sint16 axis_raw(SDL_GameControllerAxis axis)
{
    if (!input_active)
        return 0;

    Sint16 value = 0;
    if (controller) {
        SDL_GameControllerButtonBind bind =
            SDL_GameControllerGetBindForAxis(controller, axis);
        if (bind.bindType != SDL_CONTROLLER_BINDTYPE_NONE)
            value = SDL_GameControllerGetAxis(controller, axis);
    } else if (raw_joystick) {
        static const int raw_axis[SDL_CONTROLLER_AXIS_MAX] = {
            [SDL_CONTROLLER_AXIS_LEFTX] = 0, [SDL_CONTROLLER_AXIS_LEFTY] = 1,
            [SDL_CONTROLLER_AXIS_RIGHTX] = 2, [SDL_CONTROLLER_AXIS_RIGHTY] = 3,
            [SDL_CONTROLLER_AXIS_TRIGGERLEFT] = -1,
            [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = -1,
        };
        int index = (axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX)
                  ? raw_axis[axis] : -1;
        if (index >= 0 && index < SDL_JoystickNumAxes(raw_joystick))
            value = SDL_JoystickGetAxis(raw_joystick, index);
        else if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
                 SDL_JoystickNumButtons(raw_joystick) > 6)
            value = SDL_JoystickGetButton(raw_joystick, 6) ? INT16_MAX : 0;
        else if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
                 SDL_JoystickNumButtons(raw_joystick) > 7)
            value = SDL_JoystickGetButton(raw_joystick, 7) ? INT16_MAX : 0;
    }
    return value;
}

static void apply_joystick_hat(SDL_Joystick *joy)
{
    if (!joy || SDL_JoystickNumHats(joy) <= 0)
        return;
    Uint8 hat = SDL_JoystickGetHat(joy, 0);
    if (hat & SDL_HAT_UP)    buttons[SDL_CONTROLLER_BUTTON_DPAD_UP] = 1;
    if (hat & SDL_HAT_DOWN)  buttons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = 1;
    if (hat & SDL_HAT_LEFT)  buttons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 1;
    if (hat & SDL_HAT_RIGHT) buttons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = 1;
}

static void read_buttons(void)
{
    memcpy(previous, buttons, sizeof buttons);
    memset(buttons, 0, sizeof buttons);
    if (!input_active) {
        evdev_pad.trigger_state[0] = evdev_pad.trigger_state[1] = 0;
        return;
    }
    if (controller && evdev_pad.semantic) {
        /* Mapping estrangeiro: nenhum botao do SDL_GameController e'
         * confiavel; o snapshot evdev preenche tudo, e o hat cru cobre um
         * dpad declarado como ABS_HAT em vez de BTN_DPAD_*. */
        apply_joystick_hat(SDL_GameControllerGetJoystick(controller));
    } else if (controller) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
            buttons[i] = SDL_GameControllerGetButton(controller,
                                                     (SDL_GameControllerButton)i);
    } else if (raw_joystick) {
        int n = SDL_JoystickNumButtons(raw_joystick);
        for (int i = 0; i < n && i < (int)(sizeof raw_to_pad /
                                           sizeof *raw_to_pad); i++)
            if (raw_to_pad[i] >= 0 && SDL_JoystickGetButton(raw_joystick, i))
                buttons[raw_to_pad[i]] = 1;
        apply_joystick_hat(raw_joystick);
    }
    apply_evdev_snapshot();
    apply_trigger_happy_fallback();
}

/* ------------------------------------------------------------- abrir o pad */

static void announce(const char *physical, int vendor, int product)
{
    /* O InControl casa o perfil pelo NOME reportado pelo InputDevice.  O perfil
     * Xbox 360 e' o que cobre o layout completo (4 botoes, 2 sticks, 2 gatilhos,
     * dpad) e existe nas duas familias de perfil compiladas neste jogo. */
    sc_jni_input_device_info("Microsoft X-Box 360 pad", vendor, product,
                             physical && *physical ? physical
                                                   : "nextos-gamepad");
}

static void open_controller(void)
{
    if (controller || raw_joystick)
        return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i))
            continue;
        controller = SDL_GameControllerOpen(i);
        if (!controller)
            continue;
        SDL_Joystick *joy = SDL_GameControllerGetJoystick(controller);
        const char *physical = joy ? SDL_JoystickName(joy)
                                   : SDL_GameControllerName(controller);
        int vendor = joy ? SDL_JoystickGetVendor(joy) : 0;
        int product = joy ? SDL_JoystickGetProduct(joy) : 0;
        controller_instance = joy ? SDL_JoystickInstanceID(joy) : -1;
        announce(physical, vendor, product);
        fprintf(stderr, "[sc/input] pad: %s (%04x:%04x)\n",
                physical ? physical : "desconhecido", vendor & 0xffff,
                product & 0xffff);
        char *mapping = SDL_GameControllerMapping(controller);
        if (mapping) {
            fprintf(stderr, "[sc/input] SDL mapping: %s\n", mapping);
            SDL_free(mapping);
        }
        if (!open_evdev_pad(physical, vendor, product))
            find_trigger_happy_ordinals(physical, vendor, product);
        return;
    }
    if (SDL_NumJoysticks() > 0) {
        raw_joystick = SDL_JoystickOpen(0);
        if (raw_joystick) {
            const char *name = SDL_JoystickName(raw_joystick);
            const int vendor = SDL_JoystickGetVendor(raw_joystick);
            const int product = SDL_JoystickGetProduct(raw_joystick);
            controller_instance = SDL_JoystickInstanceID(raw_joystick);
            announce(name, vendor, product);
            fprintf(stderr,
                    "[sc/input] pad CRU: \"%s\" (%d botoes, %d eixos, %d hats)"
                    " -- sem mapeamento na base do SDL\n",
                    name ? name : "desconhecido",
                    SDL_JoystickNumButtons(raw_joystick),
                    SDL_JoystickNumAxes(raw_joystick),
                    SDL_JoystickNumHats(raw_joystick));
            if (!open_evdev_pad(name, vendor, product))
                find_trigger_happy_ordinals(name, vendor, product);
        }
    }
}

static void close_controller(void)
{
    close_evdev_pad();
    th_select_ordinal = th_start_ordinal = -1;
    controller_instance = -1;
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    if (raw_joystick) {
        SDL_JoystickClose(raw_joystick);
        raw_joystick = NULL;
    }
}

/* --------------------------------------------------- roteiro de teste (debug)
 * SC_INPUTTEST="600:start:6,900:dpright:120,960:a:4" segura o botao indicado a
 * partir do quadro dado, pelo numero de quadros pedido.  Serve so' para PROVAR
 * no laboratorio a cadeia que vai do estado do pad ate' o InControl, sem
 * ninguem apertando nada.  NAO e' o caminho do jogo: o release nunca define
 * essa variavel e o pad fisico continua sendo a unica fonte de entrada. */
#define TEST_MAX 16
static struct { unsigned long frame, until; int button; } test_steps[TEST_MAX];
static int test_n;

static int button_by_name(const char *name, size_t len)
{
    static const struct { const char *n; int b; } names[] = {
        { "a", SDL_CONTROLLER_BUTTON_A }, { "b", SDL_CONTROLLER_BUTTON_B },
        { "x", SDL_CONTROLLER_BUTTON_X }, { "y", SDL_CONTROLLER_BUTTON_Y },
        { "start", SDL_CONTROLLER_BUTTON_START },
        { "select", SDL_CONTROLLER_BUTTON_BACK },
        { "dpup", SDL_CONTROLLER_BUTTON_DPAD_UP },
        { "dpdown", SDL_CONTROLLER_BUTTON_DPAD_DOWN },
        { "dpleft", SDL_CONTROLLER_BUTTON_DPAD_LEFT },
        { "dpright", SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
        { "l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
        { "r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (strlen(names[i].n) == len && strncmp(names[i].n, name, len) == 0)
            return names[i].b;
    return -1;
}

static void parse_test_script(void)
{
    const char *s = getenv("SC_INPUTTEST");
    if (!s || !*s)
        return;
    while (*s && test_n < TEST_MAX) {
        char *end = NULL;
        unsigned long frame = strtoul(s, &end, 10);
        if (!end || *end != ':')
            break;
        const char *name = end + 1;
        const char *stop = name;
        while (*stop && *stop != ':' && *stop != ',')
            stop++;
        int button = button_by_name(name, (size_t)(stop - name));
        unsigned long hold = 6;
        if (*stop == ':')
            hold = strtoul(stop + 1, &end, 10);
        else
            end = (char *)stop;
        if (button >= 0 && hold > 0) {
            test_steps[test_n].frame = frame;
            test_steps[test_n].until = frame + hold;
            test_steps[test_n].button = button;
            fprintf(stderr, "[sc/input] teste: quadro %lu..%lu botao %.*s\n",
                    frame, frame + hold, (int)(stop - name), name);
            test_n++;
        }
        while (*end && *end != ',')
            end++;
        s = *end == ',' ? end + 1 : end;
    }
}

static void apply_test_script(unsigned long frame)
{
    for (int i = 0; i < test_n; i++)
        if (frame >= test_steps[i].frame && frame < test_steps[i].until)
            buttons[test_steps[i].button] = 1;
}

/* -------------------------------------------------------------- injecao */

static void inject(void *env, void *player, void *event)
{
    static void *native_inject;
    static int looked_up;
    if (!looked_up) {
        native_inject = sc_jni_native("com/unity3d/player/UnityPlayer",
                                      "nativeInjectEvent");
        looked_up = 1;
    }
    if (!native_inject || !event) {
        if (input_diag)
            fprintf(stderr, "[sc/input] inject PULADO fn=%p event=%p\n",
                    native_inject, event);
        return;
    }
    /* Unity 2017.4 registra nativeInjectEvent(InputEvent) -- tres argumentos
     * nativos, sem displayId (que so' aparece a partir da 2019). */
    uint8_t consumed = ((uint8_t (*)(void *, void *, void *))native_inject)(
        env, player, event);
    if (input_diag)
        fprintf(stderr, "[sc/input] inject event=%p consumed=%u\n", event,
                consumed);
}

/* nxinput entra como OBSERVADOR: ele ve os mesmos eventos SDL e publica o
 * recibo de entrada do contrato.  A traducao para KeyEvent/MotionEvent do
 * Android continua sendo deste arquivo -- nada do caminho ja validado muda. */
static nxinput_context *sc_nxinput;

int sc_input_init(void)
{
    const char *v = getenv("SC_INPUTLOG");
    input_diag = v && *v && *v != '0';
    const char *face = getenv("SC_FACE_LAYOUT");
    configured_face_policy = FACE_POLICY_AUTO;
    if (face && *face && strcmp(face, "auto") != 0) {
        if (strcmp(face, "firmware") == 0)
            configured_face_policy = FACE_POLICY_FIRMWARE;
        else if (strcmp(face, "xbox") == 0)
            configured_face_policy = FACE_POLICY_XBOX;
        else
            fprintf(stderr,
                    "[sc/input] SC_FACE_LAYOUT=%s invalido; usando auto\n",
                    face);
    }
    input_active = 1;
    exit_requested = 0;
    memset(buttons, 0, sizeof buttons);
    memset(previous, 0, sizeof previous);
    memset(trigger_down, 0, sizeof trigger_down);
    memset(trigger_prev, 0, sizeof trigger_prev);
    reset_motion_filters();
    motion_non_neutral = 0;
    motion_neutral_pending = 0;
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0 &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "[sc/input] SDL_InitSubSystem(GAMECONTROLLER): %s\n",
                SDL_GetError());
    parse_test_script();
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    open_controller();
    if (!controller && !raw_joystick)
        fprintf(stderr,
                "[sc/input] nenhum pad conectado ainda; hotplug ligado\n");

    nxinput_config config;
    nxinput_config_init(&config);
    config.stick_enter_deadzone = SC_INPUT_STICK_ENTER_DEADZONE;
    config.stick_exit_deadzone = SC_INPUT_STICK_EXIT_DEADZONE;
    config.trigger_deadzone = SC_INPUT_TRIGGER_DEADZONE;
    sc_nxinput = nxinput_create(&config);
    if (!sc_nxinput)
        fprintf(stderr, "[sc/input] nxinput indisponivel: %s\n",
                SDL_GetError());
    else {
        /* O Suzy Cube nao usa cursor: a UI e' toda navegada pelo pad. */
        nxinput_set_cursor_context(sc_nxinput, NXINPUT_CURSOR_OFF);
        if (sc_framework_publish_input(sc_nxinput) != 0)
            fprintf(stderr, "[sc/framework] recibo de entrada recusado\n");
    }
    return 0;
}

void sc_input_poll(void *env, void *player, unsigned long frame)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (sc_nxinput)
            nxinput_observe_event(sc_nxinput, &ev);
        switch (ev.type) {
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_JOYDEVICEADDED:
            if (!controller && !raw_joystick)
                open_controller();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (ev.cdevice.which == controller_instance) {
                request_neutral_motion();
                close_controller();
                open_controller();
            }
            break;
        case SDL_JOYDEVICEREMOVED:
            if (ev.jdevice.which == controller_instance) {
                request_neutral_motion();
                close_controller();
                open_controller();
            }
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                request_neutral_motion();
                input_active = 0;
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                input_active = 1;
            }
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
            request_neutral_motion();
            input_active = 0;
            break;
        case SDL_APP_WILLENTERFOREGROUND:
        case SDL_APP_DIDENTERFOREGROUND:
            input_active = 1;
            break;
        case SDL_QUIT:
            exit_requested = 1;
            break;
        default:
            break;
        }
    }

    /* Atualiza primeiro, depois consulta. O snapshot evdev logo abaixo torna
     * os botoes autocorretivos mesmo se um CFW perder uma transicao SDL. */
    SDL_JoystickUpdate();
    if (sc_nxinput)
        nxinput_poll(sc_nxinput);

    read_buttons();
    apply_test_script(frame);

    if (test_n == 0 && (!input_active || (!controller && !raw_joystick))) {
        int release_pending = trigger_down[0] || trigger_down[1] ||
                              motion_non_neutral || motion_neutral_pending;
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
            release_pending |= previous[i] != 0;
        if (!release_pending)
            return;
    }

    /* SELECT+START: saida pelo ciclo de vida nativo (pause + save + fim). */
    if (buttons[SDL_CONTROLLER_BUTTON_BACK] &&
        buttons[SDL_CONTROLLER_BUTTON_START]) {
        if (!exit_requested)
            fprintf(stderr, "[sc/input] SELECT+START: saindo pelo caminho "
                            "nativo\n");
        exit_requested = 1;
        return;
    }

    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        if (buttons[i] == previous[i] || android_key[i] == 0)
            continue;
        inject(env, player,
               sc_jni_key_event(buttons[i] ? 0 : 1, android_key[i], 0));
    }

    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    nxinput_core_filter_stick(
        &stick_filter[0], axis_raw(SDL_CONTROLLER_AXIS_LEFTX),
        axis_raw(SDL_CONTROLLER_AXIS_LEFTY),
        SC_INPUT_STICK_ENTER_DEADZONE, SC_INPUT_STICK_EXIT_DEADZONE,
        &lx, &ly);
    nxinput_core_filter_stick(
        &stick_filter[1], axis_raw(SDL_CONTROLLER_AXIS_RIGHTX),
        axis_raw(SDL_CONTROLLER_AXIS_RIGHTY),
        SC_INPUT_STICK_ENTER_DEADZONE, SC_INPUT_STICK_EXIT_DEADZONE,
        &rx, &ry);

    /* SDL_GameController standardizes triggers to [0, 32767], including
     * mappings such as lefttrigger:b6/righttrigger:b7. Released digital
     * triggers therefore return 0, not -32768; treating that 0 as a signed
     * midpoint made both released triggers look 50% pressed on ROCKNIX. */
    float lt = nxinput_core_trigger(
        axis_raw(SDL_CONTROLLER_AXIS_TRIGGERLEFT),
        SC_INPUT_TRIGGER_DEADZONE);
    float rt = nxinput_core_trigger(
        axis_raw(SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
        SC_INPUT_TRIGGER_DEADZONE);
    if (evdev_pad.semantic) {
        /* O bind de gatilho pertence ao mapping estrangeiro; o estado
         * digital de BTN_TL2/TR2 do proprio evdev e' a fonte. */
        lt = evdev_pad.trigger_state[0] ? 1.0f : 0.0f;
        rt = evdev_pad.trigger_state[1] ? 1.0f : 0.0f;
    }

    /* Gatilhos analogicos tambem geram L2/R2 digitais. */
    trigger_prev[0] = trigger_down[0];
    trigger_prev[1] = trigger_down[1];
    trigger_down[0] = lt > 0.5f;
    trigger_down[1] = rt > 0.5f;
    if (trigger_down[0] != trigger_prev[0])
        inject(env, player,
               sc_jni_key_event(trigger_down[0] ? 0 : 1, KEY_BUTTON_L2, 0));
    if (trigger_down[1] != trigger_prev[1])
        inject(env, player,
               sc_jni_key_event(trigger_down[1] ? 0 : 1, KEY_BUTTON_R2, 0));

    /* O dpad tambem viaja como HAT no MotionEvent: e' assim que o perfil
     * Android do InControl le direcao, alem dos KEYCODE_DPAD_*. */
    float hat_x = (buttons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] ? 1.0f : 0.0f) -
                  (buttons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] ? 1.0f : 0.0f);
    float hat_y = (buttons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] ? 1.0f : 0.0f) -
                  (buttons[SDL_CONTROLLER_BUTTON_DPAD_UP] ? 1.0f : 0.0f);

    const int current_non_neutral =
        lx != 0.0f || ly != 0.0f || rx != 0.0f || ry != 0.0f ||
        lt != 0.0f || rt != 0.0f || hat_x != 0.0f || hat_y != 0.0f;
    if (motion_neutral_pending && current_non_neutral)
        inject(env, player,
               sc_jni_motion_event(0.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f));
    inject(env, player,
           sc_jni_motion_event(lx, ly, rx, ry, lt, rt, hat_x, hat_y));
    motion_non_neutral = current_non_neutral;
    motion_neutral_pending = 0;
}

void sc_input_close(void)
{
    close_controller();
    if (sc_nxinput) {
        nxinput_destroy(sc_nxinput);
        sc_nxinput = NULL;
    }
}
