//
// Copyright(C) 2024 RP2040 Doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//     Bluetooth gamepad support (Pico 2 W builds only)
//
#ifndef I_BLUETOOTH_H
#define I_BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>

// Bluepad32's normalised (Xbox style) gamepad layout
#define BT_DPAD_UP      0x01
#define BT_DPAD_DOWN    0x02
#define BT_DPAD_RIGHT   0x04
#define BT_DPAD_LEFT    0x08
#define BT_BUTTON_A     0x0001 // bottom face button
#define BT_BUTTON_B     0x0002 // right
#define BT_BUTTON_X     0x0004 // left
#define BT_BUTTON_Y     0x0008 // top
#define BT_BUTTON_LB    0x0010
#define BT_BUTTON_RB    0x0020
#define BT_BUTTON_LT    0x0040 // digital trigger bits (also see brake/throttle)
#define BT_BUTTON_RT    0x0080
#define BT_BUTTON_L3    0x0100
#define BT_BUTTON_R3    0x0200
#define BT_MISC_SYSTEM  0x01
#define BT_MISC_SELECT  0x02
#define BT_MISC_START   0x04
#define BT_AXIS_MAX     512   // sticks are -512..511
#define BT_TRIGGER_MAX  1024  // brake/throttle are 0..1023

// latest controller report; written from the Bluetooth interrupt on core 0, read on core 1.
// seq is odd while a write is in progress (seqlock)
typedef struct {
    uint32_t seq;
    bool connected;
    uint8_t dpad;
    uint8_t misc;
    uint16_t buttons;
    int16_t axis_x, axis_y, axis_rx, axis_ry;
    int16_t brake, throttle;
} bt_pad_state_t;

extern volatile bt_pad_state_t bt_pad;

// bring up the radio and Bluepad32 (call once core 1, sound and graphics are running)
void I_BluetoothInit(void);

#endif
