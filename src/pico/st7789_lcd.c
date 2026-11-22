/*
 * Copyright (c) 2024 RP2040 Doom contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The panel register values used below follow those used by Pimoroni's ST7789 driver
 * for their 320x240 Pico Display Pack 2.0 / 2.8 (https://github.com/pimoroni/pimoroni-pico).
 */
#include "st7789_lcd.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#include "pico/binary_info.h"
#include "st7789_lcd.pio.h"

bi_decl(bi_program_feature("ST7789 SPI LCD output"));
bi_decl(bi_4pins_with_names(LCD_PIN_DC, "LCD DC", LCD_PIN_CS, "LCD CS", LCD_PIN_SCK, "LCD SCK", LCD_PIN_MOSI, "LCD MOSI"));
bi_decl(bi_1pin_with_name(LCD_PIN_BL, "LCD backlight"));

uint lcd_sm;
static uint lcd_pio_offset;

enum {
    ST7789_SWRESET = 0x01,
    ST7789_SLPOUT = 0x11,
    ST7789_INVON = 0x21,
    ST7789_DISPON = 0x29,
    ST7789_CASET = 0x2A,
    ST7789_RASET = 0x2B,
    ST7789_RAMWR = 0x2C,
    ST7789_TEON = 0x35,
    ST7789_MADCTL = 0x36,
    ST7789_COLMOD = 0x3A,
    ST7789_RAMCTRL = 0xB0,
    ST7789_PORCTRL = 0xB2,
    ST7789_GCTRL = 0xB7,
    ST7789_VCOMS = 0xBB,
    ST7789_LCMCTRL = 0xC0,
    ST7789_VDVVRHEN = 0xC2,
    ST7789_VRHS = 0xC3,
    ST7789_VDVS = 0xC4,
    ST7789_FRCTRL2 = 0xC6,
    ST7789_PWCTRL1 = 0xD0,
    ST7789_GMCTRP1 = 0xE0,
    ST7789_GMCTRN1 = 0xE1,
};

enum {
    MADCTL_ROW_ORDER = 0x80,
    MADCTL_COL_ORDER = 0x40,
    MADCTL_SWAP_XY = 0x20,
    MADCTL_SCAN_ORDER = 0x10,
};

static void lcd_sm_init(uint bits) {
    pio_sm_set_enabled(LCD_PIO, lcd_sm, false);
    pio_sm_config c = st7789_lcd_program_get_default_config(lcd_pio_offset);
    sm_config_set_sideset_pins(&c, LCD_PIN_SCK);
    sm_config_set_out_pins(&c, LCD_PIN_MOSI, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    // two PIO cycles per bit. Use a whole number divider (rounded up, so never faster than LCD_SPI_HZ): a
    // fractional one jitters, and at 270MHz would give some SCK half periods of ~7ns, the ST7789's limit.
    // (No floating point: the Doom builds are linked without a float library.)
    uint32_t hz2 = 2u * (uint32_t) LCD_SPI_HZ;
    uint32_t div = (clock_get_hz(clk_sys) + hz2 - 1) / hz2;
    if (div < 1) div = 1;
    sm_config_set_clkdiv_int_frac(&c, (uint16_t) div, 0);
    sm_config_set_out_shift(&c, false, true, bits);
    pio_sm_init(LCD_PIO, lcd_sm, lcd_pio_offset, &c);
    pio_sm_set_enabled(LCD_PIO, lcd_sm, true);
}

static void lcd_write_bytes(const uint8_t *data, uint len) {
    for (uint i = 0; i < len; i++) {
        pio_sm_put_blocking(LCD_PIO, lcd_sm, ((uint32_t) data[i]) << 24u);
    }
}

static void lcd_command(uint8_t cmd, uint len, const char *data) {
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    lcd_write_bytes(&cmd, 1);
    lcd_wait_idle();
    if (len) {
        gpio_put(LCD_PIN_DC, 1);
        lcd_write_bytes((const uint8_t *) data, len);
        lcd_wait_idle();
    }
    gpio_put(LCD_PIN_CS, 1);
}

static void lcd_set_window(uint x0, uint y0, uint w, uint h) {
    uint x1 = x0 + w - 1;
    uint y1 = y0 + h - 1;
    char caset[4] = {(char) (x0 >> 8), (char) x0, (char) (x1 >> 8), (char) x1};
    char raset[4] = {(char) (y0 >> 8), (char) y0, (char) (y1 >> 8), (char) y1};
    lcd_command(ST7789_CASET, 4, caset);
    lcd_command(ST7789_RASET, 4, raset);
}

static void lcd_fill_black(void) {
    lcd_set_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
    lcd_command(ST7789_RAMWR, 0, NULL);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    for (uint i = 0; i < LCD_WIDTH * LCD_HEIGHT * 2; i++) {
        pio_sm_put_blocking(LCD_PIO, lcd_sm, 0);
    }
    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);
}

void lcd_init(uint y0, uint rows) {
    // the Display Pack's RGB LED (where present on these pins) is active low; turn it off
#ifdef LCD_PIN_LED_R
    const uint led_pins[] = {LCD_PIN_LED_R, LCD_PIN_LED_G, LCD_PIN_LED_B};
    for (uint i = 0; i < count_of(led_pins); i++) {
        gpio_init(led_pins[i]);
        gpio_put(led_pins[i], 1);
        gpio_set_dir(led_pins[i], GPIO_OUT);
    }
#endif
    gpio_init(LCD_PIN_BL);
    gpio_put(LCD_PIN_BL, 0);
    gpio_set_dir(LCD_PIN_BL, GPIO_OUT);

    gpio_init(LCD_PIN_CS);
    gpio_init(LCD_PIN_DC);
    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 1);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);

#if LCD_PIN_RESET >= 0
    gpio_init(LCD_PIN_RESET);
    gpio_set_dir(LCD_PIN_RESET, GPIO_OUT);
    gpio_put(LCD_PIN_RESET, 0);
    busy_wait_ms(5);
    gpio_put(LCD_PIN_RESET, 1);
    busy_wait_ms(120);
#endif

    lcd_sm = (uint) pio_claim_unused_sm(LCD_PIO, true);
    lcd_pio_offset = pio_add_program(LCD_PIO, &st7789_lcd_program);
    pio_gpio_init(LCD_PIO, LCD_PIN_MOSI);
    pio_gpio_init(LCD_PIO, LCD_PIN_SCK);
    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_MOSI, 1, true);
    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_SCK, 1, true);
    lcd_sm_init(8);

    lcd_command(ST7789_SWRESET, 0, NULL);
    busy_wait_ms(150);
    lcd_command(ST7789_TEON, 0, NULL);
    lcd_command(ST7789_COLMOD, 1, "\x05"); // 16 bits per pixel
    lcd_command(ST7789_PORCTRL, 5, "\x0c\x0c\x00\x33\x33");
    lcd_command(ST7789_LCMCTRL, 1, "\x2c");
    lcd_command(ST7789_VDVVRHEN, 1, "\x01");
    lcd_command(ST7789_VRHS, 1, "\x12");
    lcd_command(ST7789_VDVS, 1, "\x20");
    lcd_command(ST7789_PWCTRL1, 2, "\xa4\xa1");
    lcd_command(ST7789_FRCTRL2, 1, "\x0f"); // 60Hz panel refresh
    lcd_command(ST7789_RAMCTRL, 2, "\x00\xc0");
    lcd_command(ST7789_GCTRL, 1, "\x35");
    lcd_command(ST7789_VCOMS, 1, "\x1f");
    lcd_command(ST7789_GMCTRP1, 14, "\xD0\x08\x11\x08\x0C\x15\x39\x33\x50\x36\x13\x14\x29\x2D");
    lcd_command(ST7789_GMCTRN1, 14, "\xD0\x08\x10\x08\x06\x06\x39\x44\x51\x0B\x16\x14\x2F\x31");
    lcd_command(ST7789_INVON, 0, NULL);
    lcd_command(ST7789_SLPOUT, 0, NULL);
    lcd_command(ST7789_DISPON, 0, NULL);
    busy_wait_ms(100);

    char madctl = (char) ((LCD_ROTATE_180 ? MADCTL_ROW_ORDER : MADCTL_COL_ORDER) | MADCTL_SWAP_XY | MADCTL_SCAN_ORDER);
    lcd_command(ST7789_MADCTL, 1, &madctl);

    lcd_fill_black();
    lcd_set_window(0, y0, LCD_WIDTH, rows);

    // from now on everything is sent as 16 bit units
    lcd_sm_init(16);
    gpio_put(LCD_PIN_CS, 0);

    dma_channel_claim(lcd_dma_chan);
    dma_channel_config c = dma_channel_get_default_config(lcd_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(LCD_PIO, lcd_sm, true));
    dma_channel_configure(lcd_dma_chan, &c, &LCD_PIO->txf[lcd_sm], NULL, 0, false);
    dma_channel_set_irq0_enabled(lcd_dma_chan, true);

    busy_wait_ms(20);
    gpio_put(LCD_PIN_BL, 1);
}
