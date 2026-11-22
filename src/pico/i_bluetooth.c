//
// Copyright(C) 2024 RP2040 Doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//     Bluetooth gamepad support (Pico 2 W) via Bluepad32 + BTstack.
//
//     BTstack runs in the cyw43 "threadsafe background" async context: everything
//     happens in a low priority interrupt on core 0, so the game loop never has to
//     call it. Controller reports are copied into a small snapshot (a seqlock, so the
//     reader never sees a half written report) which core 1's 2ms input tick turns
//     into Doom key events alongside the Display Pack buttons (see i_buttons.c).
//
//     Core 0 has its interrupts off while Doom writes a saved game, which just pauses
//     Bluetooth briefly; nothing here runs on core 1.
//

#include <string.h>
#include "pico/cyw43_arch.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "pico/binary_info.h"
#include <uni.h>
#include "i_bluetooth.h"

bi_decl(bi_program_feature("Bluetooth gamepad support (Bluepad32)"));

volatile bt_pad_state_t bt_pad;
static int connected_count;

static void publish(const uni_gamepad_t *gp, bool connected) {
    uint32_t seq = bt_pad.seq;
    bt_pad.seq = seq + 1; // odd: write in progress
    __mem_fence_release();
    if (gp) {
        bt_pad.buttons = (uint16_t) gp->buttons;
        bt_pad.misc = gp->misc_buttons;
        bt_pad.dpad = gp->dpad;
        bt_pad.axis_x = (int16_t) gp->axis_x;
        bt_pad.axis_y = (int16_t) gp->axis_y;
        bt_pad.axis_rx = (int16_t) gp->axis_rx;
        bt_pad.axis_ry = (int16_t) gp->axis_ry;
        bt_pad.brake = (int16_t) gp->brake;
        bt_pad.throttle = (int16_t) gp->throttle;
    } else {
        bt_pad.buttons = 0;
        bt_pad.misc = 0;
        bt_pad.dpad = 0;
        bt_pad.axis_x = bt_pad.axis_y = bt_pad.axis_rx = bt_pad.axis_ry = 0;
        bt_pad.brake = bt_pad.throttle = 0;
    }
    bt_pad.connected = connected;
    __mem_fence_release();
    bt_pad.seq = seq + 2;
}

static void doom_bt_init(int argc, const char **argv) {
    (void) argc;
    (void) argv;
}

static void doom_bt_on_init_complete(void) {
    // look for new controllers and reconnect to paired ones; pairing keys are kept in flash
    uni_bt_start_scanning_and_autoconnect_unsafe();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t doom_bt_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi) {
    (void) addr;
    (void) name;
    (void) rssi;
    // only gamepads/joysticks (not keyboards, mice, ...)
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

static void doom_bt_on_device_connected(uni_hid_device_t *d) {
    (void) d;
}

static void doom_bt_on_device_disconnected(uni_hid_device_t *d) {
    (void) d;
    if (connected_count > 0) connected_count--;
    if (!connected_count) {
        publish(NULL, false);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    }
}

static uni_error_t doom_bt_on_device_ready(uni_hid_device_t *d) {
    (void) d;
    connected_count++;
    // the Pico 2 W's green LED shows a controller is connected
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    return UNI_ERROR_SUCCESS;
}

static void doom_bt_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    (void) d;
    if (ctl->klass == UNI_CONTROLLER_CLASS_GAMEPAD) {
        publish(&ctl->gamepad, true);
    }
}

static const uni_property_t *doom_bt_get_property(uni_property_idx_t idx) {
    (void) idx;
    return NULL;
}

static void doom_bt_on_oob_event(uni_platform_oob_event_t event, void *data) {
    (void) event;
    (void) data;
}

static struct uni_platform doom_bt_platform = {
    .name = "Doom",
    .init = doom_bt_init,
    .on_init_complete = doom_bt_on_init_complete,
    .on_device_discovered = doom_bt_on_device_discovered,
    .on_device_connected = doom_bt_on_device_connected,
    .on_device_disconnected = doom_bt_on_device_disconnected,
    .on_device_ready = doom_bt_on_device_ready,
    .on_oob_event = doom_bt_on_oob_event,
    .on_controller_data = doom_bt_on_controller_data,
    .get_property = doom_bt_get_property,
};

void I_BluetoothInit(void) {
    // Some DMA channels are used at fixed numbers without being claimed (I2C networking 9/10, the status bar
    // XIP stream 11). Claim them so the cyw43 driver, which takes unused channels, can't pick them.
    const uint fixed_channels[] = {9, 10, 11};
    for (uint i = 0; i < count_of(fixed_channels); i++) {
        if (!dma_channel_is_claimed(fixed_channels[i])) dma_channel_claim(fixed_channels[i]);
    }
    if (cyw43_arch_init()) {
        return; // no radio: carry on with just the buttons
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    uni_platform_set_custom(&doom_bt_platform);
    uni_init(0, NULL);
    // BTstack now runs from interrupts (async_context_threadsafe_background); no run loop needed here
}
