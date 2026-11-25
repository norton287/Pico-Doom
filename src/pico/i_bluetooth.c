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

// bring-up progress, drawn as a status strip by the LCD code (see i_video.c)
volatile uint8_t bt_debug_stage;      // BT_STAGE_*
volatile uint8_t bt_debug_heartbeat;  // ticks every 250ms while the Bluetooth background context runs
volatile uint8_t bt_debug_discovered; // devices seen while scanning
volatile uint8_t bt_debug_connected;  // on_device_connected calls
volatile uint8_t bt_debug_ready;      // on_device_ready calls
volatile uint8_t bt_debug_reports;    // controller reports received
volatile uint8_t bt_debug_dpad_only;  // the controller is being treated as d-pad only
volatile uint8_t bt_debug_raw;        // what the latest report contains (see i_bluetooth.h)
volatile int8_t bt_debug_init_err;    // cyw43_arch_init() result
// raw HCI traffic, counted before Bluepad32 filters anything
volatile uint8_t bt_debug_inq_done;   // inquiry (classic scan) cycles completed
volatile uint8_t bt_debug_inq_result; // classic inquiry results (any device)
volatile uint8_t bt_debug_adv;        // BLE advertising reports (any device)
volatile uint8_t bt_debug_cmd_err;    // HCI commands the radio rejected
volatile uint8_t bt_debug_conn_req;   // incoming classic connection requests

#if DOOM_BT_DEBUG
static void raw_hci_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void) channel;
    if (packet_type != HCI_EVENT_PACKET || size < 1) return;
    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_INQUIRY_COMPLETE:
        case GAP_EVENT_INQUIRY_COMPLETE:
            bt_debug_inq_done++;
            break;
        case GAP_EVENT_INQUIRY_RESULT:
            bt_debug_inq_result++;
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            bt_debug_adv++;
            break;
        case HCI_EVENT_CONNECTION_REQUEST:
            bt_debug_conn_req++;
            break;
        case HCI_EVENT_COMMAND_STATUS:
            if (size > 2 && packet[2]) bt_debug_cmd_err++;
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (size > 5 && packet[5]) bt_debug_cmd_err++;
            break;
        default:
            break;
    }
}
static btstack_packet_callback_registration_t raw_hci_registration = {.callback = raw_hci_handler};
#endif

static void heartbeat_fn(async_context_t *context, async_at_time_worker_t *worker) {
    bt_debug_heartbeat++;
    async_context_add_at_time_worker_in_ms(context, worker, 250);
}
static async_at_time_worker_t heartbeat_worker = {.do_work = heartbeat_fn};

static bool name_is_dpad_only; // recognised by name
static bool analog_seen;       // reported a real analog stick / trigger since connecting
static bool dpad_only;

// d-pad-only controllers report their d-pad as the left stick and have nothing else to turn with
static bool is_dpad_only(const uni_hid_device_t *d) {
    return strstr(d->name, "Zero") != NULL; // 8BitDo Zero / Zero 2 ("8BitDo Zero 2 gamepad")
}

static void publish(const uni_gamepad_t *gp, bool connected) {
    uint32_t seq = bt_pad.seq;
    bt_pad.seq = seq + 1; // odd: write in progress
    __mem_fence_release();
    if (gp) {
        if (!analog_seen && bt_report_is_analog(gp->axis_x, gp->axis_y, gp->axis_rx, gp->axis_ry, gp->brake, gp->throttle)) {
            analog_seen = true;
        }
        dpad_only = name_is_dpad_only || !analog_seen;
        bt_debug_dpad_only = dpad_only;
        bt_debug_raw = (uint8_t) ((gp->axis_x > BT_AXIS_MAX / 2 || gp->axis_x < -BT_AXIS_MAX / 2 ? 1 : 0) |
                                  (gp->axis_y > BT_AXIS_MAX / 2 || gp->axis_y < -BT_AXIS_MAX / 2 ? 2 : 0) |
                                  (gp->dpad ? 4 : 0) |
                                  (gp->axis_rx > BT_AXIS_MAX / 2 || gp->axis_rx < -BT_AXIS_MAX / 2 ? 8 : 0));
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
    bt_pad.dpad_only = connected && dpad_only;
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
    bt_debug_stage = BT_STAGE_SCANNING;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t doom_bt_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi) {
    (void) addr;
    (void) name;
    (void) rssi;
    bt_debug_discovered++;
    // only gamepads/joysticks (not keyboards, mice, ...)
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

static void doom_bt_on_device_connected(uni_hid_device_t *d) {
    (void) d;
    bt_debug_connected++;
}

static void doom_bt_on_device_disconnected(uni_hid_device_t *d) {
    (void) d;
    if (connected_count > 0) connected_count--;
    if (!connected_count) {
        publish(NULL, false);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        // look for a controller again (and let paired ones reconnect)
        uni_bt_start_scanning_and_autoconnect_unsafe();
    }
}

static uni_error_t doom_bt_on_device_ready(uni_hid_device_t *d) {
    bt_debug_ready++;
    // until it shows a real analog stick, treat it as d-pad only (a d-pad-as-stick looks like a stick pushed fully)
    name_is_dpad_only = is_dpad_only(d);
    analog_seen = false;
    dpad_only = true;
    bt_debug_dpad_only = dpad_only;
    connected_count++;
    // the Pico 2 W's green LED shows a controller is connected
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    // stop scanning while one is connected: classic inquiry plus BLE scanning hands core 0 every advert from
    // every nearby device, which halved Doom's frame rate (100ms vs 50ms per frame on a Pico 2 W)
    uni_bt_stop_scanning_unsafe();
    return UNI_ERROR_SUCCESS;
}

static void doom_bt_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    (void) d;
    bt_debug_reports++;
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
    bt_debug_stage = BT_STAGE_RADIO_INIT;
    int err = cyw43_arch_init();
    bt_debug_init_err = (int8_t) err;
    if (err) {
        bt_debug_stage = BT_STAGE_RADIO_FAILED;
        return; // no radio: carry on with just the buttons
    }
    bt_debug_stage = BT_STAGE_RADIO_OK;
    async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &heartbeat_worker, 250);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    uni_platform_set_custom(&doom_bt_platform);
    uni_init(0, NULL);
#if DOOM_BT_DEBUG
    // count raw radio traffic for the status strip (BTstack runs from an interrupt: take its lock)
    async_context_acquire_lock_blocking(cyw43_arch_async_context());
    hci_add_event_handler(&raw_hci_registration);
    async_context_release_lock(cyw43_arch_async_context());
#endif
    if (bt_debug_stage == BT_STAGE_RADIO_OK) bt_debug_stage = BT_STAGE_STACK_STARTED;
    // BTstack now runs from interrupts (async_context_threadsafe_background); no run loop needed here
}
