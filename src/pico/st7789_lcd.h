/*
 * Copyright (c) 2024 RP2040 Doom contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal ST7789 driver used to stream the Doom frame to a 320x240 SPI LCD
 * (e.g. Pimoroni Pico Display Pack 2.0 / 2.8) one scanline at a time.
 *
 * Pixels are written using PIO (write-only SPI) fed by a single DMA channel.
 * The hot path functions are static inline / RAM resident, because they are
 * called from core 1 while core 0 has flash XIP disabled during game saves.
 */
#ifndef ST7789_LCD_H
#define ST7789_LCD_H

#include "pico.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

// ---------------------------------------------------------------------------
// Pin / panel configuration (defaults match the Pimoroni Pico Display Pack 2.x)
// ---------------------------------------------------------------------------
#ifndef LCD_PIN_DC
#define LCD_PIN_DC 16
#endif
#ifndef LCD_PIN_CS
#define LCD_PIN_CS 17
#endif
#ifndef LCD_PIN_SCK
#define LCD_PIN_SCK 18
#endif
#ifndef LCD_PIN_MOSI
#define LCD_PIN_MOSI 19
#endif
#ifndef LCD_PIN_BL
#define LCD_PIN_BL 20
#endif
// set to -1 if the panel has no reset line wired (Display Pack ties it to RUN)
#ifndef LCD_PIN_RESET
#define LCD_PIN_RESET -1
#endif

#ifndef LCD_WIDTH
#define LCD_WIDTH 320
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 240
#endif

// Maximum SPI bit clock. The ST7789 is specified to 62.5MHz. The actual clock is clk_sys / (2 * whole number),
// so at Doom's 270MHz: 62.5MHz -> 45MHz (~37 LCD frames/s); 67500000 -> 67.5MHz (~55 fps, a little over spec
// but fine on the Display Packs we know of).
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ 62500000
#endif

// 1 = rotate the picture 180 degrees (for mounting the Pico "upside down")
#ifndef LCD_ROTATE_180
#define LCD_ROTATE_180 0
#endif

// pio1 is used by I2S audio (PICO_AUDIO_I2S_PIO=1); pio0 is free as the LCD build has no scanvideo
#ifndef LCD_PIO
#define LCD_PIO pio0
#endif
// fixed DMA channels elsewhere: 6 = I2S audio, 9/10 = I2C networking, 11 = status bar XIP stream
#ifndef LCD_DMA_CHANNEL
#define LCD_DMA_CHANNEL 0
#endif

// RGB565 as expected by the ST7789 (16 bit, sent MSB first)
#define LCD_PIXEL_FROM_RGB8(r, g, b) ((((uint)(r) >> 3u) << 11u) | (((uint)(g) >> 2u) << 5u) | ((uint)(b) >> 3u))

extern uint lcd_sm;
#define lcd_dma_chan LCD_DMA_CHANNEL

// Initialize PIO/DMA/GPIO and the panel, clears the screen to black, and leaves the
// window set to rows [y0, y0 + rows) of the full width. Call from the core which will own
// the DMA interrupt.
void lcd_init(uint y0, uint rows);

// Wait (busy) until every queued bit has left the PIO state machine.
static __force_inline void lcd_wait_idle(void) {
    uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + lcd_sm);
    LCD_PIO->fdebug = stall_mask;
    while (!(LCD_PIO->fdebug & stall_mask)) tight_loop_contents();
}

// Begin a new frame: (re)issues RAMWR so the panel's write pointer returns to the
// top-left of the window. Must only be called when no pixel DMA is in flight.
static __force_inline void lcd_begin_frame(void) {
    lcd_wait_idle();
    // toggling CS resynchronizes the panel's serial interface each frame, so a single
    // glitched bit can never leave the picture permanently corrupted
    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 0);
    // hold CS high for >40ns (the ST7789 minimum) even at 270MHz
    for (int i = 0; i < 4; i++) __asm volatile ("nop\nnop\nnop\nnop\n");
    gpio_put(LCD_PIN_CS, 0);
    // the SM is in 16 bit mode; send NOP (0x00) followed by RAMWR (0x2C)
    pio_sm_put(LCD_PIO, lcd_sm, 0x002Cu << 16u);
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 1);
}

// Start streaming `count` RGB565 pixels from `src` (must stay valid until the DMA completes)
static __force_inline void lcd_dma_send(const void *src, uint count) {
    dma_channel_hw_t *ch = &dma_hw->ch[lcd_dma_chan];
    ch->read_addr = (uintptr_t) src;
    ch->al1_transfer_count_trig = count;
}

static __force_inline void lcd_dma_ack_irq(void) {
    dma_hw->ints0 = 1u << lcd_dma_chan;
}

#endif
