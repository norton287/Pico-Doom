//
// Copyright(C) 2024 RP2040 Doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     Play Doom with just the four buttons (A, B, X, Y) of a Pimoroni
//     Pico Display Pack. Button chords are translated into ordinary Doom
//     key events, so every other input method (UART / USB keyboard) still
//     works alongside.
//
//     In game:              A = turn left       X = turn right
//                           B = forward         Y = fire
//                           A+X tap  = use (open doors, switches)
//                           A+X hold (1s) = menu (Esc)
//                           A+X held, then B  = move backward
//                           A+X held, then tap Y = next weapon
//                           all four tap      = automap
//                           all four hold 1.5s = IDDQD, hold 3s = IDKFA
//     In menus:             A = up   B = down   X = select / yes
//                           Y = close menu / no      Y held + A/B = left/right
//     ENDOOM (quit) screen: any button restarts
//
//     Bluetooth gamepad (Pico 2 W builds, Xbox style names; 8BitDo etc. map the same by position):
//       left stick / d-pad up,down = move     left stick sideways = strafe
//       right stick / d-pad left,right = turn
//       RT or X = fire     A = use     B or LT = run     LB / RB = previous / next weapon
//       Y or Select = automap     Start = menu
//       menus: d-pad/stick = navigate, A = select/yes, B = back/no, Start = close
//
//     How it runs: the game only asks for input once per tic, and when it is busy
//     those requests can be far apart. So the whole button state machine runs on
//     core 1, every 2ms, from the LCD line interrupt (which runs continuously):
//     it samples, debounces, decides chords/holds/taps with accurate timing, and
//     queues finished Doom key events. Core 0 (I_ButtonsPoll, from the game's input
//     poll) just hands the queued events to the game.
//
//     Everything core 1 runs here is RAM resident (__not_in_flash_func, no flash
//     constants) because the LCD interrupt keeps running while core 0 has flash
//     disabled to write a saved game.
//

#include "pico.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/binary_info.h"

#include "doomkeys.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_system.h"
#include "m_controls.h"
#include "doom/doomstat.h"
#include "i_buttons.h"
#if PICO_BLUETOOTH_INPUT
#include "i_bluetooth.h"
#endif

#ifndef BUTTON_PIN_A
#define BUTTON_PIN_A 12
#endif
#ifndef BUTTON_PIN_B
#define BUTTON_PIN_B 13
#endif
#ifndef BUTTON_PIN_X
#define BUTTON_PIN_X 14
#endif
#ifndef BUTTON_PIN_Y
#define BUTTON_PIN_Y 15
#endif

bi_decl(bi_program_feature("Pico Display Pack A/B/X/Y button controls"));
bi_decl(bi_4pins_with_names(BUTTON_PIN_A, "Button A", BUTTON_PIN_B, "Button B", BUTTON_PIN_X, "Button X", BUTTON_PIN_Y, "Button Y"));

// timings (microseconds)
#define MS                 1000u
#define SAMPLE_US          (2 * MS)    // sampling / state machine tick
#define CHORD_US           (40 * MS)   // a lone A/X waits this long for its partner before turning
#define TAP_US             (80 * MS)   // how long a "tapped" key is held
#define MENU_HOLD_US       (1000 * MS) // A+X hold for menu
#define CHEAT1_HOLD_US     (1500 * MS) // all four hold for IDDQD
#define CHEAT2_HOLD_US     (3000 * MS) // all four hold for IDKFA
#define REPEAT_DELAY_US    (350 * MS)  // menu key auto-repeat
#define REPEAT_RATE_US     (110 * MS)

#define BTN_A 1u
#define BTN_B 2u
#define BTN_X 4u
#define BTN_Y 8u
#define BTN_ALL (BTN_A | BTN_B | BTN_X | BTN_Y)

extern int messageToPrint;
extern boolean messageNeedsInput;

#define RAM_FUNC(f) __not_in_flash_func(f)
// time comparisons that survive the 32 bit microsecond counter wrapping
#define TIME_REACHED(now, t) ((int32_t) ((now) - (t)) >= 0)

enum {
    LAYER_GAME,
    LAYER_MENU,
    LAYER_EXIT,
};

// game "held" keys
enum {
    HELD_LEFT = 1,
    HELD_RIGHT = 2,
    HELD_FORWARD = 4,
    HELD_BACK = 8,
    HELD_FIRE = 16,
    HELD_STRAFE_LEFT = 32,
    HELD_STRAFE_RIGHT = 64,
    HELD_RUN = 128,
};
#define HELD_LAST HELD_RUN

// ---------------------------------------------------------------------------
// core 1 -> core 0 key event queue (single producer, single consumer)
// ---------------------------------------------------------------------------
#define KEY_QUEUE_SIZE 64 // power of 2
static uint16_t key_queue[KEY_QUEUE_SIZE]; // (down ? 0x100 : 0) | key
static volatile uint32_t key_queue_write;  // core 1 only
static volatile uint32_t key_queue_read;   // core 0 only

static void RAM_FUNC(emit)(bool down, uint key) {
    uint32_t w = key_queue_write;
    if (w - key_queue_read >= KEY_QUEUE_SIZE) return; // full (core 0 not polling, e.g. loading); drop
    key_queue[w & (KEY_QUEUE_SIZE - 1)] = (uint16_t) ((down ? 0x100u : 0u) | (key & 0xffu));
    __mem_fence_release();
    key_queue_write = w + 1;
}

// ---------------------------------------------------------------------------
// core 1 state machine
// ---------------------------------------------------------------------------
static uint32_t last_tick_us;
static uint8_t sample_raw_last;
static uint8_t buttons;          // debounced state
static uint8_t prev_buttons;
static uint8_t consumed;         // buttons ignored until released (after a layer change)
static uint8_t prev_live;
static uint32_t pressed_at[4];

static uint8_t layer = LAYER_GAME;
static uint8_t held_keys;        // keys currently held down in the game
static uint8_t button_want;      // held keys wanted by the Display Pack buttons
static uint8_t pad_want;         // ... and by a Bluetooth gamepad

// shift (A+X) state
static bool shift;
static bool shift_used;
static bool shift_back;          // B pressed during shift => moving backward
static bool shift_y_pending;     // Y pressed during shift => next weapon on release
static uint32_t shift_start;
static bool all_four;
static uint32_t all_four_start;
static uint8_t all_four_cheats;

// menu state
static bool menu_y_used;
static uint8_t repeat_key;
static uint32_t repeat_next;

// tapped keys waiting for release
#define MAX_TAPS 4
static struct {
    uint8_t key;
    uint32_t release_us;
} taps[MAX_TAPS];

// cheats typed one character per tick; kept in RAM (not flash string literals)
static char cheat_god[] = "iddqd";
static char cheat_all[] = "idkfa";
static const char *type_pending;

static volatile bool reboot_requested;

#if DOOM_BUTTON_DEBUG
// read by the LCD code to draw a debug strip (see i_video.c)
volatile uint8_t button_debug_shift;
volatile uint8_t button_debug_use_count;
volatile uint8_t button_debug_esc_count;
volatile uint8_t button_debug_poll_count;
volatile uint16_t button_debug_max_poll_gap_ms;
#endif

static __force_inline uint8_t read_raw(void) {
    // pins are compile time constants, so this doesn't touch flash
    uint32_t all = sio_hw->gpio_in;
    return (uint8_t) ((((all >> BUTTON_PIN_A) & 1u) ^ 1u) |
                      ((((all >> BUTTON_PIN_B) & 1u) ^ 1u) << 1u) |
                      ((((all >> BUTTON_PIN_X) & 1u) ^ 1u) << 2u) |
                      ((((all >> BUTTON_PIN_Y) & 1u) ^ 1u) << 3u));
}

static void RAM_FUNC(tap_key)(uint key, uint32_t now) {
    for (int i = 0; i < MAX_TAPS; i++) {
        if (!taps[i].key) {
            emit(true, key);
#if DOOM_BUTTON_DEBUG
            if (key == key_use) button_debug_use_count++;
            if (key == key_menu_activate) button_debug_esc_count++;
#endif
            taps[i].key = (uint8_t) key;
            taps[i].release_us = now + TAP_US;
            return;
        }
    }
}

static void RAM_FUNC(release_taps)(uint32_t now) {
    for (int i = 0; i < MAX_TAPS; i++) {
        if (taps[i].key && TIME_REACHED(now, taps[i].release_us)) {
            emit(false, taps[i].key);
            taps[i].key = 0;
        }
    }
}

static uint RAM_FUNC(held_key_code)(uint bit) {
    // (an if chain rather than a switch, which GCC may turn into a lookup table in flash)
    if (bit == HELD_LEFT) return key_left;
    if (bit == HELD_RIGHT) return key_right;
    if (bit == HELD_FORWARD) return key_up;
    if (bit == HELD_BACK) return key_down;
    if (bit == HELD_STRAFE_LEFT) return key_strafeleft;
    if (bit == HELD_STRAFE_RIGHT) return key_straferight;
    if (bit == HELD_RUN) return key_speed;
    return key_fire;
}

static void RAM_FUNC(set_held_keys)(uint8_t want) {
    uint8_t changed = want ^ held_keys;
    for (uint bit = 1; bit <= HELD_LAST; bit <<= 1) {
        if (changed & bit) emit(want & bit, held_key_code(bit));
    }
    held_keys = want;
}

static void RAM_FUNC(reset_layer_state)(void) {
    button_want = pad_want = 0;
    set_held_keys(0);
    shift = shift_used = shift_back = shift_y_pending = false;
    all_four = false;
    menu_y_used = false;
    repeat_key = 0;
    // anything still held belongs to the old layer
    consumed = buttons;
}

static void RAM_FUNC(game_layer)(uint32_t now, uint8_t live, uint8_t pressed, uint8_t released) {
    uint8_t want = 0;
    bool a = live & BTN_A, b = live & BTN_B, x = live & BTN_X, y = live & BTN_Y;

    // all four buttons: automap on tap, cheats on long hold
    if ((live & BTN_ALL) == BTN_ALL) {
        if (!all_four) {
            all_four = true;
            all_four_start = now;
            all_four_cheats = 0;
        }
        if (all_four_cheats == 0 && TIME_REACHED(now, all_four_start + CHEAT1_HOLD_US)) {
            type_pending = cheat_god;
            all_four_cheats = 1;
        } else if (all_four_cheats == 1 && TIME_REACHED(now, all_four_start + CHEAT2_HOLD_US)) {
            type_pending = cheat_all;
            all_four_cheats = 2;
        }
        shift_used = true;
        shift_y_pending = false;
        button_want = 0;
        return;
    }
    if (all_four) {
        // leaving the four button chord
        all_four = false;
        if (!all_four_cheats) tap_key(key_map_toggle, now);
        // ignore the remaining buttons until they are released
        consumed |= live;
        shift = false;
        button_want = 0;
        return;
    }

    if (a && x) {
        if (!shift) {
            shift = true;
            shift_used = false;
            shift_start = now;
            shift_back = false;
            shift_y_pending = false;
        }
        if (pressed & BTN_B) {
            shift_back = true;
            shift_used = true;
        }
        if (!b) shift_back = false;
        if (pressed & BTN_Y) {
            shift_y_pending = true;
            shift_used = true;
        }
        if ((released & BTN_Y) && shift_y_pending) {
            shift_y_pending = false;
            tap_key(key_nextweapon, now);
        }
        if (!shift_used && TIME_REACHED(now, shift_start + MENU_HOLD_US)) {
            shift_used = true;
            tap_key(key_menu_activate, now);
        }
        if (b) want |= shift_back ? HELD_BACK : HELD_FORWARD;
        if (y && !shift_y_pending) want |= HELD_FIRE;
        button_want = want;
        return;
    }
    if (shift) {
        // A or X released: end of the shift chord
        shift = false;
        if (!shift_used) tap_key(key_use, now);
        consumed |= live & (BTN_A | BTN_X);
        a = x = false;
    }

    if (a && TIME_REACHED(now, pressed_at[0] + CHORD_US)) want |= HELD_LEFT;
    if (x && TIME_REACHED(now, pressed_at[2] + CHORD_US)) want |= HELD_RIGHT;
    if (b) want |= HELD_FORWARD;
    if (y) want |= HELD_FIRE;
    button_want = want;
}

static void RAM_FUNC(menu_press)(uint key, uint32_t now, bool repeat) {
    emit(true, key);
    emit(false, key);
    repeat_key = repeat ? (uint8_t) key : 0;
    repeat_next = now + REPEAT_DELAY_US;
}

static void RAM_FUNC(menu_layer)(uint32_t now, uint8_t live, uint8_t pressed, uint8_t released) {
    bool prompt = messageToPrint && messageNeedsInput;
    bool y = live & BTN_Y;
    if (pressed & BTN_Y) menu_y_used = false;
    if (pressed & BTN_A) {
        if (y) {
            menu_y_used = true;
            menu_press(key_menu_left, now, true);
        } else {
            menu_press(key_menu_up, now, true);
        }
    }
    if (pressed & BTN_B) {
        if (y) {
            menu_y_used = true;
            menu_press(key_menu_right, now, true);
        } else {
            menu_press(key_menu_down, now, true);
        }
    }
    if (pressed & BTN_X) {
        menu_press(prompt ? key_menu_confirm : key_menu_forward, now, false);
    }
    if ((released & BTN_Y) && !menu_y_used) {
        // Esc leaves the menu from any depth (and cancels save name entry)
        menu_press(prompt ? key_menu_abort : key_menu_activate, now, false);
    }
    if (!(live & (BTN_A | BTN_B))) repeat_key = 0;
    if (repeat_key && TIME_REACHED(now, repeat_next)) {
        emit(true, repeat_key);
        emit(false, repeat_key);
        repeat_next = now + REPEAT_RATE_US;
    }
}

#if PICO_BLUETOOTH_INPUT
// ---------------------------------------------------------------------------
// Bluetooth gamepad (core 1). Sticks use a dead zone with hysteresis.
// ---------------------------------------------------------------------------
#define STICK_ON  (BT_AXIS_MAX * 40 / 100)
#define STICK_OFF (BT_AXIS_MAX * 28 / 100)
#define TRIGGER_ON (BT_TRIGGER_MAX * 30 / 100)

// virtual directions (d-pad or stick) used for menus and movement
enum {
    PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, // d-pad or left stick
    PAD_RS_LEFT = 16, PAD_RS_RIGHT = 32,                   // right stick
    PAD_LS_LEFT = 64, PAD_LS_RIGHT = 128,                  // left stick only (strafe)
};

static uint16_t pad_prev_buttons;
static uint8_t pad_prev_misc;
static uint8_t pad_dirs, pad_prev_dirs;
static uint8_t pad_repeat_dir;
static uint32_t pad_repeat_next;

static bool RAM_FUNC(read_pad)(bt_pad_state_t *out) {
    for (int tries = 0; tries < 4; tries++) {
        uint32_t seq = bt_pad.seq;
        __mem_fence_acquire();
        if (seq & 1u) continue;
        out->connected = bt_pad.connected;
        out->dpad = bt_pad.dpad;
        out->misc = bt_pad.misc;
        out->buttons = bt_pad.buttons;
        out->axis_x = bt_pad.axis_x;
        out->axis_y = bt_pad.axis_y;
        out->axis_rx = bt_pad.axis_rx;
        out->brake = bt_pad.brake;
        out->throttle = bt_pad.throttle;
        __mem_fence_acquire();
        if (bt_pad.seq == seq) return true;
    }
    return false;
}

static __force_inline bool RAM_FUNC(stick)(int v, bool was_on, bool negative) {
    if (negative) v = -v;
    return v > (was_on ? STICK_OFF : STICK_ON);
}

static uint RAM_FUNC(pad_dir_key)(uint8_t dir) {
    if (dir == PAD_UP) return key_menu_up;
    if (dir == PAD_DOWN) return key_menu_down;
    if (dir == PAD_LEFT) return key_menu_left;
    return key_menu_right;
}

static void RAM_FUNC(pad_process)(uint32_t now) {
    bt_pad_state_t p;
    if (!read_pad(&p)) return; // mid update; try again next tick
    if (!p.connected) {
        p.buttons = 0;
        p.misc = 0;
        p.dpad = 0;
        p.axis_x = p.axis_y = p.axis_rx = 0;
        p.brake = p.throttle = 0;
    }
    uint8_t d = 0, prev = pad_dirs;
    if ((p.dpad & BT_DPAD_UP) || stick(p.axis_y, prev & PAD_UP, true)) d |= PAD_UP;
    if ((p.dpad & BT_DPAD_DOWN) || stick(p.axis_y, prev & PAD_DOWN, false)) d |= PAD_DOWN;
    bool ls_left = stick(p.axis_x, prev & PAD_LS_LEFT, true);
    bool ls_right = stick(p.axis_x, prev & PAD_LS_RIGHT, false);
    if (ls_left) d |= PAD_LS_LEFT;
    if (ls_right) d |= PAD_LS_RIGHT;
    if ((p.dpad & BT_DPAD_LEFT) || ls_left) d |= PAD_LEFT;
    if ((p.dpad & BT_DPAD_RIGHT) || ls_right) d |= PAD_RIGHT;
    if (stick(p.axis_rx, prev & PAD_RS_LEFT, true)) d |= PAD_RS_LEFT;
    if (stick(p.axis_rx, prev & PAD_RS_RIGHT, false)) d |= PAD_RS_RIGHT;
    pad_prev_dirs = pad_dirs;
    pad_dirs = d;

    uint16_t pressed = p.buttons & ~pad_prev_buttons;
    uint8_t misc_pressed = p.misc & ~pad_prev_misc;
    uint8_t dir_pressed = pad_dirs & ~pad_prev_dirs;
    pad_prev_buttons = p.buttons;
    pad_prev_misc = p.misc;

    if (layer == LAYER_EXIT) {
        if (pressed || misc_pressed) reboot_requested = true;
        return;
    }
    if (layer == LAYER_MENU) {
        bool prompt = messageToPrint && messageNeedsInput;
        for (int i = 0; i < 4; i++) {
            uint8_t dir = (uint8_t) (1u << i); // PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT (no flash table)
            if (dir_pressed & dir) {
                uint key = pad_dir_key(dir);
                emit(true, key);
                emit(false, key);
                pad_repeat_dir = dir;
                pad_repeat_next = now + REPEAT_DELAY_US;
            }
        }
        if (!(pad_dirs & pad_repeat_dir)) pad_repeat_dir = 0;
        if (pad_repeat_dir && TIME_REACHED(now, pad_repeat_next)) {
            uint key = pad_dir_key(pad_repeat_dir);
            emit(true, key);
            emit(false, key);
            pad_repeat_next = now + REPEAT_RATE_US;
        }
        if (pressed & BT_BUTTON_A) {
            emit(true, prompt ? key_menu_confirm : key_menu_forward);
            emit(false, prompt ? key_menu_confirm : key_menu_forward);
        }
        if ((pressed & BT_BUTTON_B) || (misc_pressed & BT_MISC_START)) {
            emit(true, prompt ? key_menu_abort : key_menu_activate);
            emit(false, prompt ? key_menu_abort : key_menu_activate);
        }
        pad_want = 0;
        return;
    }

    // in game
    uint8_t want = 0;
    if (pad_dirs & PAD_UP) want |= HELD_FORWARD;
    if (pad_dirs & PAD_DOWN) want |= HELD_BACK;
    if (pad_dirs & PAD_LS_LEFT) want |= HELD_STRAFE_LEFT;
    if (pad_dirs & PAD_LS_RIGHT) want |= HELD_STRAFE_RIGHT;
    if ((p.dpad & BT_DPAD_LEFT) || (pad_dirs & PAD_RS_LEFT)) want |= HELD_LEFT;
    if ((p.dpad & BT_DPAD_RIGHT) || (pad_dirs & PAD_RS_RIGHT)) want |= HELD_RIGHT;
    if ((p.buttons & (BT_BUTTON_RT | BT_BUTTON_X)) || p.throttle > TRIGGER_ON) want |= HELD_FIRE;
    if ((p.buttons & (BT_BUTTON_LT | BT_BUTTON_B)) || p.brake > TRIGGER_ON) want |= HELD_RUN;
    pad_want = want;

    if (pressed & BT_BUTTON_A) tap_key(key_use, now);
    if (pressed & BT_BUTTON_LB) tap_key(key_prevweapon, now);
    if (pressed & BT_BUTTON_RB) tap_key(key_nextweapon, now);
    if ((pressed & BT_BUTTON_Y) || (misc_pressed & BT_MISC_SELECT)) tap_key(key_map_toggle, now);
    if (misc_pressed & BT_MISC_START) tap_key(key_menu_activate, now);
}
#endif

static void RAM_FUNC(process)(uint32_t now, uint8_t state) {
    buttons = state;
    uint8_t pressed = buttons & ~prev_buttons;
    for (int i = 0; i < 4; i++) {
        if (pressed & (1u << i)) pressed_at[i] = now;
    }
    consumed &= buttons;
    prev_buttons = buttons;

    release_taps(now);

    if (type_pending) {
        if (*type_pending) {
            emit(true, (uint8_t) *type_pending);
            emit(false, (uint8_t) *type_pending);
            type_pending++;
        } else {
            type_pending = NULL;
        }
    }

    uint8_t new_layer = at_exit_screen ? LAYER_EXIT : (menuactive ? LAYER_MENU : LAYER_GAME);
    if (new_layer != layer) {
        layer = new_layer;
        reset_layer_state();
    }

    // edges are computed from the live (non consumed) view so a consumed button's release never triggers anything
    uint8_t live = buttons & ~consumed;
    uint8_t live_pressed = live & ~prev_live;
    uint8_t live_released = prev_live & ~live;
    prev_live = live;

    if (layer == LAYER_EXIT) {
        if (live_pressed) reboot_requested = true;
    } else if (layer == LAYER_MENU) {
        menu_layer(now, live, live_pressed, live_released);
    } else {
        game_layer(now, live, live_pressed, live_released);
    }
#if PICO_BLUETOOTH_INPUT
    pad_process(now);
#endif
    set_held_keys(layer == LAYER_GAME ? (uint8_t) (button_want | pad_want) : 0);
#if DOOM_BUTTON_DEBUG
    button_debug_shift = shift;
#endif
}

// core 1, every LCD line: cheap unless a 2ms tick is due
void RAM_FUNC(I_ButtonsSample)(void) {
    uint32_t now = time_us_32();
    if ((uint32_t) (now - last_tick_us) < SAMPLE_US) return;
    last_tick_us = now;
    // a change is only accepted once two consecutive samples (2ms apart) agree
    uint8_t raw = read_raw();
    uint8_t state = raw == sample_raw_last ? raw : buttons;
    sample_raw_last = raw;
    process(now, state);
}

// ---------------------------------------------------------------------------
// core 0
// ---------------------------------------------------------------------------
static const uint8_t button_pins[4] = {BUTTON_PIN_A, BUTTON_PIN_B, BUTTON_PIN_X, BUTTON_PIN_Y};

// key ups whose key went down in the same poll: the game only looks at which keys are down once per
// tic (after each poll), so those are held back until the next poll or a quick tap would be invisible
#define MAX_DEFERRED 8
static uint8_t deferred_up[MAX_DEFERRED];
static uint8_t num_deferred;
// the event queue in d_event.c is only 8 deep in the small builds; leave headroom for UART/USB keys
#define MAX_EVENTS_PER_POLL 6

void I_ButtonsInit(void) {
    for (int i = 0; i < 4; i++) {
        gpio_init(button_pins[i]);
        gpio_set_dir(button_pins[i], GPIO_IN);
        gpio_pull_up(button_pins[i]); // buttons short to ground
    }
}

static void post_key(evtype_t type, int key) {
    event_t ev = {0};
    ev.type = type;
    ev.data1 = key;
    if (type == ev_keydown) {
        // data2/data3 carry the typed character (used by cheats and text entry)
        ev.data2 = key;
        ev.data3 = key;
    }
    D_PostEvent(&ev);
}

void I_ButtonsPoll(void) {
#if DOOM_BUTTON_DEBUG
    static uint32_t last_poll_us, gap_window_start;
    static uint16_t window_max;
    uint32_t t = time_us_32();
    uint32_t gap_ms = (t - last_poll_us) / 1000u;
    last_poll_us = t;
    if (gap_ms > window_max) window_max = (uint16_t) (gap_ms > 0xffff ? 0xffff : gap_ms);
    if (t - gap_window_start > 1000000u) {
        // show the longest gap between input polls over the last second
        button_debug_max_poll_gap_ms = window_max;
        window_max = 0;
        gap_window_start = t;
    }
    button_debug_poll_count++;
#endif
    if (reboot_requested) {
        watchdog_reboot(0, 0, 1);
    }

    int budget = MAX_EVENTS_PER_POLL;
    // releases held back from the previous poll go first
    int n = num_deferred;
    num_deferred = 0;
    for (int i = 0; i < n; i++) {
        post_key(ev_keyup, deferred_up[i]);
        budget--;
    }

    uint8_t down_this_poll[MAX_EVENTS_PER_POLL];
    int num_down = 0;
    uint32_t w = key_queue_write;
    __mem_fence_acquire();
    while (key_queue_read != w && budget > 0) {
        uint16_t e = key_queue[key_queue_read & (KEY_QUEUE_SIZE - 1)];
        uint8_t key = (uint8_t) e;
        if (e & 0x100u) {
            post_key(ev_keydown, key);
            if (num_down < MAX_EVENTS_PER_POLL) down_this_poll[num_down++] = key;
        } else {
            bool same_poll = false;
            for (int i = 0; i < num_down; i++) {
                if (down_this_poll[i] == key) same_poll = true;
            }
            if (same_poll && num_deferred < MAX_DEFERRED) {
                deferred_up[num_deferred++] = key;
            } else {
                post_key(ev_keyup, key);
            }
        }
        budget--;
        key_queue_read++;
    }
}
