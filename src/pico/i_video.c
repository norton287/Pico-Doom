//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
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
//	DOOM graphics stuff for Pico.
//

#if PICODOOM_RENDER_NEWHOPE
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <doom/r_data.h>
#include "doom/f_wipe.h"
#include "pico.h"

#include "config.h"
#include "d_loop.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_diskicon.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "st7789_lcd.h"
#if PICO_BUTTON_INPUT
#include "i_buttons.h"
#if PICO_BLUETOOTH_INPUT
#include "i_bluetooth.h"
#endif
#include "hardware/structs/sio.h"
#endif
#include "hardware/irq.h"
#include "hardware/sync.h"
#define VIDEO_PIXEL_FROM_RGB8 LCD_PIXEL_FROM_RGB8
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "picodoom.h"
#include "image_decoder.h"
#if PICO_ON_DEVICE
#include "hardware/dma.h"
#include "hardware/structs/xip_ctrl.h"
#endif

#define YELLOW_SUBMARINE 0
#define SUPPORT_TEXT 1
#if SUPPORT_TEXT
typedef struct __packed {
    const char * const name;
    const uint8_t * const data;
    const uint8_t w;
    const uint8_t h;
} txt_font_t;
#define TXT_SCREEN_W 80
#include "fonts/normal.h"

static uint16_t ega_colors[] = {
    VIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0x00),         // 0: Black
    VIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0xa8),         // 1: Blue
    VIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0x00),         // 2: Green
    VIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0xa8),         // 3: Cyan
    VIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0x00),         // 4: Red
    VIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0xa8),         // 5: Magenta
    VIDEO_PIXEL_FROM_RGB8(0xa8, 0x54, 0x00),         // 6: Brown
    VIDEO_PIXEL_FROM_RGB8(0xa8, 0xa8, 0xa8),         // 7: Grey
    VIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0x54),         // 8: Dark grey
    VIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0xfe),         // 9: Bright blue
    VIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0x54),         // 10: Bright green
    VIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0xfe),         // 11: Bright cyan
    VIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0x54),         // 12: Bright red
    VIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0xfe),         // 13: Bright magenta
    VIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0x54),         // 14: Yellow
    VIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0xfe),         // 15: Bright white
};
#endif

// todo temproarly turned this off because it causes a seeming bug in scanvideo (perhaps only with the new callback stuff) where the last repeated scanline of a pixel line is freed while shown
//  note it may just be that this happens anyway, but usually we are writing slower than the beam?
#define USE_INTERP PICO_ON_DEVICE
#if USE_INTERP
#include "hardware/interp.h"
#endif

CU_REGISTER_DEBUG_PINS(scanline_copy)
//CU_SELECT_DEBUG_PINS(scanline_copy)

static const patch_t *stbar;

volatile uint8_t interp_in_use;

#define USE_1280x1024x60 1

// display has been set up?

static boolean initialized = false;

boolean screenvisible = true;

//int vga_porch_flash = false;

//static int startup_delay = 1000;

// The screen buffer; this is modified to draw things to the screen
//pixel_t *I_VideoBuffer = NULL;
// Gamma correction level to use

boolean screensaver_mode = false;

isb_int8_t usegamma = 0;

// Joystick/gamepad hysteresis
unsigned int joywait = 0;

pixel_t *I_VideoBuffer; // todo can't have this

uint8_t __aligned(4) frame_buffer[2][SCREENWIDTH*MAIN_VIEWHEIGHT];
static uint16_t palette[256];
static uint16_t __scratch_x("shared_pal") shared_pal[NUM_SHARED_PALETTES][16];
static int8_t next_pal=-1;

semaphore_t render_frame_ready, display_frame_freed;
semaphore_t core1_launch;

uint8_t *text_screen_data;
static uint32_t *text_scanline_buffer_start;
static uint8_t *text_screen_cpy;
static uint8_t *text_font_cpy;


#if USE_INTERP
static interp_hw_save_t interp0_save, interp1_save;
static boolean interp_updated;
static boolean need_save;

static inline void interp_save_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    saver->accum[0] = interp->accum[0];
    saver->accum[1] = interp->accum[1];
    saver->base[0] = interp->base[0];
    saver->base[1] = interp->base[1];
    saver->base[2] = interp->base[2];
    saver->ctrl[0] = interp->ctrl[0];
    saver->ctrl[1] = interp->ctrl[1];
}

static inline void interp_restore_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    interp->accum[0] = saver->accum[0];
    interp->accum[1] = saver->accum[1];
    interp->base[0] = saver->base[0];
    interp->base[1] = saver->base[1];
    interp->base[2] = saver->base[2];
    interp->ctrl[0] = saver->ctrl[0];
    interp->ctrl[1] = saver->ctrl[1];
}
#endif

void I_ShutdownGraphics(void)
{
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}

//
// Set the window title
//

void I_SetWindowTitle(const char *title)
{
//    window_title = title;
}

//
// I_SetPalette
//
void I_SetPaletteNum(int doompalette)
{
    next_pal = doompalette;
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
}

uint8_t display_frame_index;
uint8_t display_overlay_index;
uint8_t display_video_type;

typedef void (*scanline_func)(uint32_t *dest, int scanline);

static void scanline_func_none(uint32_t *dest, int scanline);
static void scanline_func_double(uint32_t *dest, int scanline);
static void scanline_func_single(uint32_t *dest, int scanline);
static void scanline_func_wipe(uint32_t *dest, int scanline);

scanline_func scanline_funcs[] = {
        scanline_func_none,     // VIDEO_TYPE_NONE
        NULL,                   // VIDEO_TYPE_TEXT
        scanline_func_single,   // VIDEO_TYPE_SAVING
        scanline_func_double,   // VIDEO_TYPE_DOUBLE
        scanline_func_single,   // VIDEO_TYPE_SINGLE
        scanline_func_wipe,     // VIDEO_TYPE_WIPE
};

uint8_t *wipe_yoffsets; // position of start of y in each column
int16_t *wipe_yoffsets_raw;
uint32_t *wipe_linelookup; // offset of each line from start of screenbuffer (can be negative for FB 1 to FB 0)
uint8_t next_video_type;
uint8_t next_frame_index; // todo combine with video type?
uint8_t next_overlay_index;
#if !DEMO1_ONLY
uint8_t *next_video_scroll;
uint8_t *video_scroll;
#endif
volatile uint8_t wipe_min;
uint32_t *saved_scanline_buffer_ptrs[PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT];

#pragma GCC push_options
#if PICO_ON_DEVICE
#pragma GCC optimize("O3")
#endif

static inline void palette_convert_scanline(uint32_t *dest, const uint8_t *src) {
#if USE_INTERP
    if (interp_updated != 1) {
                if (need_save) {
                    interp_save_static(interp0, &interp0_save);
                    interp_save_static(interp1, &interp1_save);
                }
                interp_config c = interp_default_config();
                interp_config_set_shift(&c, 0);
                interp_config_set_mask(&c, 0, 7);
                interp_set_config(interp0, 0, &c);
                interp_config_set_shift(&c, 16);
                interp_set_config(interp1, 0, &c);
                interp_config_set_shift(&c, 8);
                interp_config_set_cross_input(&c, true);
                interp_set_config(interp0, 1, &c);
                interp_config_set_shift(&c, 24);
                interp_set_config(interp1, 1, &c);
                uint32_t palette_div2 = ((uintptr_t)palette) >> 1;
                interp0->base[0] = palette_div2;
                interp0->base[1] = palette_div2;
                interp1->base[0] = palette_div2;
                interp1->base[1] = palette_div2;
                interp_updated = 1;
            }
            extern void palette8to16(uint32_t *dest, const uint8_t *src, uint words);
            palette8to16(dest, src, SCREENWIDTH);
//            dest[4] = (255-scanline) * 0x2000;
            dest += SCREENWIDTH / 2;
//            dest[-4] = (255-scanline) * 0x10001;
#else
    for (int i = 0; i < SCREENWIDTH; i += 2) {
        uint32_t val = palette[*src++];
        val |= (palette[*src++]) << 16;
        *dest++ = val;
    }
#endif
}
static void scanline_func_none(uint32_t *dest, int scanline) {
    memset(dest, 0, SCREENWIDTH * 2);
}


static void __scratch_x("scanlines") scanline_func_double(uint32_t *dest, int scanline) {
    if (scanline < MAIN_VIEWHEIGHT) {
        const uint8_t *src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;
//        if (scanline == 100) {
//            printf("SL %d %p\n", display_frame_index, &frame_buffer[display_frame_index]);
//        }
        palette_convert_scanline(dest, src);
    } else {
        // we expect everything to be overdrawn by statusbar so we do nothing
    }
}

static void __not_in_flash_func(scanline_func_single)(uint32_t *dest, int scanline) {
    uint8_t *src;
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;
    } else {
        src = frame_buffer[display_frame_index^1] + (scanline - 32) * SCREENWIDTH;
    }
#if !DEMO1_ONLY
    if (video_scroll) {
        for(int i=SCREENWIDTH-1;i>0;i--) {
            src[i] = src[i-1];
        }
        src[0] = video_scroll[scanline];
    }
#endif
    palette_convert_scanline(dest, src);
}

static void scanline_func_wipe(uint32_t *dest, int scanline) {
    const uint8_t *src;
#if 0
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index^1] + scanline * SCREENWIDTH;
    } else {
        src = frame_buffer[display_frame_index] + (scanline - 32) * SCREENWIDTH;
    }
    palette_convert_scanline(dest, src);
    return;
#endif
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index];
    } else {
        src = frame_buffer[display_frame_index^1] - 32 * SCREENWIDTH;
    }
    assert(wipe_yoffsets && wipe_linelookup);
    uint16_t *d = (uint16_t *)dest;
    src += scanline * SCREENWIDTH;
    for (int i = 0; i < SCREENWIDTH; i++) {
        int rel = scanline - wipe_yoffsets[i];
        if (rel < 0) {
            d[i] = palette[src[i]];
        } else {
            const uint8_t *flip;
#if PICO_ON_DEVICE
            flip = (const uint8_t *)wipe_linelookup[rel];
#else
            flip = &frame_buffer[0][0] + wipe_linelookup[rel];
#endif
            // todo better protection here
            if (flip >= &frame_buffer[0][0] && flip < &frame_buffer[0][0] + 2 * SCREENWIDTH * MAIN_VIEWHEIGHT) {
                d[i] = palette[flip[i]];
            }
        }
    }
}

static inline uint draw_vpatch(uint16_t *dest, patch_t *patch, vpatchlist_t *vp, uint off) {
    int repeat = vp->entry.repeat;
    dest += vp->entry.x;
    int w = vpatch_width(patch);
    const uint8_t *data0 = vpatch_data(patch);
    const uint8_t *data = data0 + off;
    if (!vpatch_has_shared_palette(patch)) {
        const uint8_t *pal = vpatch_palette(patch);
        switch (vpatch_type(patch)) {
            case vp4_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 1; i < len; i += 2) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0xf]];
                        *p++ = palette[pal[v >> 4]];
                    }
                    if (len & 1) {
                        *p++ = palette[pal[(*data++) & 0xf]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                    if (v >> 4) p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp4_solid: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                    p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp6_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 3; i < len; i += 4) {
                        uint v = *data++;
                        v |= (*data++) << 8;
                        v |= (*data++) << 16;
                        *p++ = palette[pal[v & 0x3f]];
                        *p++ = palette[pal[(v >> 6) & 0x3f]];
                        *p++ = palette[pal[(v >> 12) & 0x3f]];
                        *p++ = palette[pal[(v >> 18) & 0x3f]];
                    }
                    len &= 3;
                    if (len--) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0x3f]];
                        if (len--) {
                            v >>= 6;
                            v |= (*data++) << 2;
                            *p++ = palette[pal[v & 0x3f]];
                            if (len--) {
                                v >>= 6;
                                v |= (*data++) << 4;
                                *p++ = palette[pal[v & 0x3f]];
                                assert(!len);
                            }
                        }
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp8_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 0; i < len; i++) {
                        *p++ = palette[pal[*data++]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp_border: {
                dest[0] = palette[*data++];
                uint16_t col = palette[*data++];
                for (int i = 1; i < w - 1; i++) dest[i] = col;
                dest[w-1] = palette[*data++];
                break;
            }
            default:
                assert(false);
                break;
        }
    } else {
        uint sp = vpatch_shared_palette(patch);
        uint16_t *pal16 = shared_pal[sp];
        assert(sp < NUM_SHARED_PALETTES);
        switch (vpatch_type(patch)) {
            case vp4_solid: {
#if PICO_ON_DEVICE
                if (patch == stbar) {
                    static const uint8_t *cached_data;
#if PICO_RP2040
                    static uint32_t __scratch_x("data_cache") data_cache[41];
#else
                    // short of scratch space on RP2350 for some reason, so lets put this in main RAM
                    static uint32_t data_cache[41];
#endif
                    int i = 0;
                    uint32_t *d = (uint32_t *) dest;
#define DMA_CHANNEL 11
                    if (cached_data == data) {
                        const uint8_t *source = (const uint8_t *) data_cache;
                        // we need to correct for the misalignment of data, because the XIP copy ignores the low 2 bits...
                        // the raw bitmap data is always misaligned by 3 (the size of the header in the case of stbar)
                        source += 3;
                        for (; source < (const uint8_t *) dma_hw->ch[DMA_CHANNEL].al1_write_addr; source++) {
                            uint32_t val = pal16[source[0] & 0xf];
                            val |= (pal16[source[0] >> 4]) << 16;
                            *d++ = val;
                        }
                        source -= 3;
                        i = (source - (const uint8_t *) data_cache);
                    }
                    if (true) {
                        //                        once = true;
                        xip_ctrl_hw->stream_ctr = 0;
                        // workaround yucky bug
#if !PICO_RP2350
                        (void) *(io_rw_32 *) XIP_NOCACHE_NOALLOC_BASE;
                        xip_ctrl_hw->stream_fifo;
#endif
                        dma_channel_abort(DMA_CHANNEL);
                        dma_channel_config c = dma_channel_get_default_config(DMA_CHANNEL);
                        channel_config_set_read_increment(&c, false);
                        channel_config_set_write_increment(&c, true);
                        channel_config_set_dreq(&c, DREQ_XIP_STREAM);
#if !PICO_RP2350
                        dma_channel_set_read_addr(DMA_CHANNEL, (void *) XIP_AUX_BASE, false);
#else
                        dma_channel_set_read_addr(DMA_CHANNEL, &xip_ctrl_hw->stream_fifo, false);
#endif
                        dma_channel_set_config(DMA_CHANNEL, &c, false);
                        cached_data = data + SCREENWIDTH / 2;
                        xip_ctrl_hw->stream_addr = (uintptr_t) cached_data;
                        xip_ctrl_hw->stream_ctr = 41;
                        __compiler_memory_barrier();
                        dma_channel_transfer_to_buffer_now(DMA_CHANNEL, data_cache, 41);
                    }
                    for (; i < SCREENWIDTH / 2; i++) {
                        uint32_t val = pal16[data[i] & 0xf];
                        val |= (pal16[data[i] >> 4]) << 16;
                        *d++ = val;
                    }
                    data += SCREENWIDTH / 2;
                    break; // early break from switch
                }
#endif
                if (((uintptr_t)dest)&3) {
                    uint16_t *p = dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        p[0] = pal16[v & 0xf];
                        p[1] = pal16[v >> 4];
                        p += 2;
                    }
                } else {
                    uint32_t *wide = (uint32_t *) dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        wide[i] = pal16[v & 0xf] | (pal16[v >> 4] << 16);
                    }
                }
                if (w & 1) {
                    uint v = *data++;
                    dest[w-1] = pal16[v & 0xf];
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                    if (v >> 4) p[1] = pal16[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                }
                break;
            }
            default:
                assert(false);
        }
    }
    if (repeat) {
        // we need them to be solid... which they are, but if not you'll just get some visual funk
        //assert(vpatch_type(patch) == vp4_solid);
        if (vp->entry.patch_handle == VPATCH_M_THERMM) w--; // hackity hack
        for(int i=0;i<repeat*w;i++) {
            dest[w+i] = dest[i];
        }
    }
    return data - data0;
}

// Draw the vpatch overlays (status bar, menus, HUD text, ...) which intersect this scanline on top of
// the already palette converted pixels in dest
static __force_inline void draw_scanline_overlays(uint16_t *dest, int scanline) {
    assert(scanline < count_of(vpatchlists->vpatch_starters));
    int prev = 0;
    for (int vp = vpatchlists->vpatch_starters[scanline]; vp;) {
        int next = vpatchlists->vpatch_next[vp];
        while (vpatchlists->vpatch_next[prev] && vpatchlists->vpatch_next[prev] < vp) {
            prev = vpatchlists->vpatch_next[prev];
        }
        assert(prev != vp);
        assert(vpatchlists->vpatch_next[prev] != vp);
        vpatchlists->vpatch_next[vp] = vpatchlists->vpatch_next[prev];
        vpatchlists->vpatch_next[prev] = vp;
        prev = vp;
        vp = next;
    }
    vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
    prev = 0;
    for (int vp = vpatchlists->vpatch_next[prev]; vp; vp = vpatchlists->vpatch_next[prev]) {
        patch_t *patch = resolve_vpatch_handle(overlays[vp].entry.patch_handle);
        int yoff = scanline - overlays[vp].entry.y;
        if (yoff < vpatch_height(patch)) {
            vpatchlists->vpatch_doff[vp] = draw_vpatch(dest, patch, &overlays[vp],
                                                       vpatchlists->vpatch_doff[vp]);
            prev = vp;
        } else {
            vpatchlists->vpatch_next[prev] = vpatchlists->vpatch_next[vp];
        }
    }
}

// this is not in flash as quite large and only once per frame
void __noinline new_frame_init_overlays_palette_and_wipe() {
    // re-initialize our overlay drawing
    if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
        memset(vpatchlists->vpatch_next, 0, sizeof(vpatchlists->vpatch_next));
        memset(vpatchlists->vpatch_starters, 0, sizeof(vpatchlists->vpatch_starters));
        memset(vpatchlists->vpatch_doff, 0, sizeof(vpatchlists->vpatch_doff));
        vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
        // do it in reverse so our linked lists are in ascending order
        for (int i = overlays->header.size - 1; i > 0; i--) {
            assert(overlays[i].entry.y < count_of(vpatchlists->vpatch_starters));
            vpatchlists->vpatch_next[i] = vpatchlists->vpatch_starters[overlays[i].entry.y];
            vpatchlists->vpatch_starters[overlays[i].entry.y] = i;
        }
        if (next_pal != -1) {
            static const uint8_t *playpal;
            static bool calculate_palettes;
            if (!playpal) {
                lumpindex_t l = W_GetNumForName("PLAYPAL");
                playpal = W_CacheLumpNum(l, PU_STATIC);
                calculate_palettes = W_LumpLength(l) == 768;
            }
            if (!calculate_palettes || !next_pal) {
                const uint8_t *doompalette = playpal + next_pal * 768;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    if (usegamma) {
                        r = gammatable[usegamma-1][r];
                        g = gammatable[usegamma-1][g];
                        b = gammatable[usegamma-1][b];
                    }
                    palette[i] = VIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            } else {
                int mul, r0, g0, b0;
                if (next_pal < 9) {
                    mul = next_pal * 65536 / 9;
                    r0 = 255; g0 = b0 = 0;
                } else if (next_pal < 13) {
                    mul = (next_pal - 8) * 65536 / 8;
                    r0 = 215; g0 = 186; b0 = 69;
                } else {
                    mul = 65536 / 8;
                    r0 = b0 = 0; g0 = 256;
                }
                const uint8_t *doompalette = playpal;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    r += ((r0 - r) * mul) >> 16;
                    g += ((g0 - g) * mul) >> 16;
                    b += ((b0 - b) * mul) >> 16;
                    palette[i] = VIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            }
            next_pal = -1;
            assert(vpatch_type(stbar) == vp4_solid); // no transparent, no runs, 4 bpp
            for (int i = 0; i < NUM_SHARED_PALETTES; i++) {
                patch_t *patch = resolve_vpatch_handle(vpatch_for_shared_palette[i]);
                assert(vpatch_colorcount(patch) <= 16);
                assert(vpatch_has_shared_palette(patch));
                for (int j = 0; j < 16; j++) {
                    shared_pal[i][j] = palette[vpatch_palette(patch)[j]];
                }
            }
        }
        if (display_video_type == VIDEO_TYPE_WIPE) {
//            printf("WIPEMIN %d\n", wipe_min);
            if (wipe_min <= 200) {
                bool regular = display_overlay_index; // just happens to toggle every frame
                int new_wipe_min = 200;
                for (int i = 0; i < SCREENWIDTH; i++) {
                    int v;
                    if (wipe_yoffsets_raw[i] < 0) {
                        if (regular) {
                            wipe_yoffsets_raw[i]++;
                        }
                        v = 0;
                    } else {
                        int dy = (wipe_yoffsets_raw[i] < 16) ? (1 + wipe_yoffsets_raw[i] + regular) / 2 : 4;
                        if (wipe_yoffsets_raw[i] + dy > 200) {
                            v = 200;
                        } else {
                            wipe_yoffsets_raw[i] += dy;
                            v = wipe_yoffsets_raw[i];
                        }
                    }
                    wipe_yoffsets[i] = v;
                    if (v < new_wipe_min) new_wipe_min = v;
                }
                assert(new_wipe_min >= wipe_min);
                wipe_min = new_wipe_min;
            }
        }
    }
}

// this method moved out of scratchx because we didn't have quite enough space for core1 stack
void __no_inline_not_in_flash_func(new_frame_stuff)() {
    // this part of the per frame code is in RAM as it is needed during save
    if (sem_available(&render_frame_ready)) {
        sem_acquire_blocking(&render_frame_ready);
        display_video_type = next_video_type;
        display_frame_index = next_frame_index;
        display_overlay_index = next_overlay_index;
#if !DEMO1_ONLY
        video_scroll = next_video_scroll; // todo does this waste too much space
#endif
        sem_release(&display_frame_freed);
    } else {
#if !DEMO1_ONLY
        video_scroll = NULL;
#endif
    }
    if (display_video_type != VIDEO_TYPE_SAVING) {
        // this stuff is large (so in flash) and not needed in save move
        new_frame_init_overlays_palette_and_wipe();
    }
}

// ---------------------------------------------------------------------------
// ST7789 SPI LCD output (Pimoroni Pico Display Pack 2.0 / 2.8)
//
// Rather than generating scanlines just in time for a VGA beam, core 1 renders
// each 320 pixel line into a small ring of RGB565 line buffers which a DMA
// channel streams to the panel via PIO. There is no beam to race, so if the
// renderer falls behind the panel simply waits.
//
// Everything on this path is RAM resident because it keeps running while core 0
// has flash XIP disabled to write saved games (VIDEO_TYPE_SAVING).
// ---------------------------------------------------------------------------

// 1: stretch 320x200 to 320x240 (every 5th line doubled) - the 4:3 picture Doom was drawn for
// 0: show 320x200 1:1 with black bars at top and bottom
#ifndef LCD_ASPECT_CORRECT
#define LCD_ASPECT_CORRECT 1
#endif
#if LCD_ASPECT_CORRECT
#define LCD_ROWS 240
#define LCD_Y0 0
#else
#define LCD_ROWS SCREENHEIGHT
#define LCD_Y0 ((LCD_HEIGHT - SCREENHEIGHT) / 2)
#endif
#define LCD_NUM_LINE_BUFFERS 3

#if PICO_RP2350
#define LCD_FILL_IRQ SPARE_IRQ_0
#else
#define LCD_FILL_IRQ 31 // unused IRQ on RP2040, used as a software interrupt
#endif

static uint32_t lcd_line_buffers[LCD_NUM_LINE_BUFFERS][SCREENWIDTH / 2];
static uint8_t lcd_line_repeats[LCD_NUM_LINE_BUFFERS];
static volatile uint8_t lcd_queue_count;  // line buffers queued for/in DMA
static uint8_t lcd_queue_head;            // next/current buffer to send (DMA IRQ owns)
static uint8_t lcd_queue_tail;            // next buffer to fill (fill IRQ owns)
static uint8_t lcd_repeats_left;          // extra sends of the head buffer (DMA IRQ owns)
static volatile bool lcd_dma_active;
static bool lcd_in_frame;
static uint16_t lcd_src_line;
static uint8_t lcd_line_mod5;
static uint32_t lcd_frame_number;

static __force_inline void lcd_pend_fill(void) {
#if !PICO_RP2350
    *((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ISPR_OFFSET)) = 1u << LCD_FILL_IRQ;
#else
    nvic_hw->ispr[LCD_FILL_IRQ / 32] = 1u << (LCD_FILL_IRQ % 32);
#endif
}

static __force_inline void lcd_start_head(void) {
    lcd_repeats_left = lcd_line_repeats[lcd_queue_head] - 1;
    lcd_dma_active = true;
    lcd_dma_send(lcd_line_buffers[lcd_queue_head], SCREENWIDTH);
}

static void __not_in_flash_func(lcd_dma_irq_handler)(void) {
    lcd_dma_ack_irq();
    if (lcd_repeats_left) {
        lcd_repeats_left--;
        lcd_dma_send(lcd_line_buffers[lcd_queue_head], SCREENWIDTH);
        return;
    }
    lcd_queue_head = lcd_queue_head == LCD_NUM_LINE_BUFFERS - 1 ? 0 : lcd_queue_head + 1;
    if (--lcd_queue_count) {
        lcd_start_head();
    } else {
        lcd_dma_active = false;
    }
    lcd_pend_fill();
}

#if SUPPORT_TEXT
// The 80x25 text screen (ENDOOM) is 640x400 at 8x16 per character; squash each glyph to 4x8 by OR-ing
// pixel pairs so that it fits 320x200 whilst keeping thin strokes visible.
static void lcd_render_text_scanline(uint16_t *d, int scanline) {
    const uint8_t *text_data = text_screen_data + TXT_SCREEN_W * 2 * (scanline / 8);
    const uint8_t *font_base = text_font_cpy + (scanline & 7) * 2;
    uint blink = lcd_frame_number & 16;
    for (int i = 0; i < TXT_SCREEN_W; i++) {
        uint fg = text_data[1] & 0xf;
        uint bg = (text_data[1] >> 4) & 0xf;
        if (bg & 0x8) {
            bg &= ~0x8;
            if (blink) fg = bg;
        }
        uint16_t fgc = ega_colors[fg];
        uint16_t bgc = ega_colors[bg];
        const uint8_t *glyph = font_base + text_data[0] * 16;
        uint bits = glyph[0] | glyph[1];
        bits |= bits >> 1;
        d[0] = (bits & 0x01) ? fgc : bgc;
        d[1] = (bits & 0x04) ? fgc : bgc;
        d[2] = (bits & 0x10) ? fgc : bgc;
        d[3] = (bits & 0x40) ? fgc : bgc;
        d += 4;
        text_data += 2;
    }
}
#endif

static void __not_in_flash_func(lcd_render_scanline)(uint32_t *dest, int scanline) {
    if (display_video_type == VIDEO_TYPE_TEXT) {
#if SUPPORT_TEXT
        if (text_screen_data && text_font_cpy) {
            lcd_render_text_scanline((uint16_t *) dest, scanline);
            return;
        }
#endif
        scanline_func_none(dest, scanline);
        return;
    }
    scanline_funcs[display_video_type](dest, scanline);
    if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
        draw_scanline_overlays((uint16_t *) dest, scanline);
    }
#if DOOM_BUTTON_DEBUG
    // debug strip, top right: [A][B][X][Y] raw buttons (white = down), [S] shift (green),
    // [U] colour changes on every "use" tap, [M] on every Esc tap, [P] on every input poll
    if (scanline >= 2 && scanline < 10) {
        extern volatile uint8_t button_debug_shift, button_debug_use_count, button_debug_esc_count, button_debug_poll_count;
        uint8_t raw = (uint8_t) ((((sio_hw->gpio_in >> BUTTON_PIN_A) & 1u) ^ 1u) | ((((sio_hw->gpio_in >> BUTTON_PIN_B) & 1u) ^ 1u) << 1u) |
                                 ((((sio_hw->gpio_in >> BUTTON_PIN_X) & 1u) ^ 1u) << 2u) | ((((sio_hw->gpio_in >> BUTTON_PIN_Y) & 1u) ^ 1u) << 3u));
        static const uint16_t cyc[4] = {0xF800, 0xFFE0, 0x001F, 0xF81F};
        uint16_t box[7];
        for (int i = 0; i < 4; i++) box[i] = (raw >> i) & 1u ? 0xFFFF : 0x2104;
        box[4] = button_debug_shift ? 0x07E0 : 0x2104;
        box[5] = cyc[button_debug_use_count & 3u];
        box[6] = cyc[button_debug_esc_count & 3u];
        uint16_t *d = (uint16_t *) dest + SCREENWIDTH - 8 * 8;
        for (int b = 0; b < 8; b++) {
            uint16_t c = b < 7 ? box[b] : cyc[button_debug_poll_count & 3u];
            for (int i = 0; i < 6; i++) d[b * 8 + i] = c;
            d[b * 8 + 6] = d[b * 8 + 7] = 0;
        }
    }
    // below it, a bar 1 pixel per ms long: the longest gap between the game's input polls in the last second
    // (green < 50ms, yellow < 150ms, red beyond)
    if (scanline >= 12 && scanline < 16) {
        extern volatile uint16_t button_debug_max_poll_gap_ms;
        uint gap = button_debug_max_poll_gap_ms;
        uint16_t c = gap < 50 ? 0x07E0 : gap < 150 ? 0xFFE0 : 0xF800;
        if (gap > SCREENWIDTH) gap = SCREENWIDTH;
        uint16_t *d = (uint16_t *) dest;
        for (uint i = 0; i < gap; i++) d[i] = c;
    }
    // more bars: [cyan] longest frame (1px/ms), [magenta] most queued tics (20px each),
    // [white] turn press -> first turning ticcmd (1px/ms, latest press)
    // [yellow] average frame (1px/ms)
    if (scanline >= 18 && scanline < 40 && (scanline - 18) % 6 < 4) {
        extern volatile uint16_t button_debug_max_frame_ms, button_debug_turn_latency_ms, button_debug_avg_frame_ms;
        extern volatile uint8_t button_debug_max_queued_tics;
        int bar = (scanline - 18) / 6;
        uint len = bar == 0 ? button_debug_max_frame_ms : bar == 1 ? button_debug_max_queued_tics * 20u
                 : bar == 2 ? button_debug_turn_latency_ms : button_debug_avg_frame_ms;
        uint16_t c = bar == 0 ? 0x07FF : bar == 1 ? 0xF81F : bar == 2 ? 0xFFFF : 0xFFE0;
        if (len > SCREENWIDTH) len = SCREENWIDTH;
        uint16_t *d = (uint16_t *) dest;
        for (uint i = 0; i < len; i++) d[i] = c;
        // tick marks every 50px so lengths can be read off
        for (uint i = 50; i < SCREENWIDTH; i += 50) d[i] = 0;
    }
#endif
#if PICO_BLUETOOTH_INPUT && DOOM_BT_DEBUG
    // Bluetooth bring-up strip, top left: [stage][heartbeat][discovered][connected][ready][reports]
    // stage: grey not started, yellow radio init, red radio failed, blue radio ok, magenta stack starting,
    // green scanning. The others are dark until their counter moves, then change colour on every step.
    if (scanline >= 2 && scanline < 10) {
        static const uint16_t stage_col[6] = {0x8410, 0xFFE0, 0xF800, 0x001F, 0xF81F, 0x07E0};
        static const uint16_t cyc[4] = {0xF800, 0xFFE0, 0x07FF, 0xF81F};
        // then a gap and, from raw radio traffic: [scan cycles][classic devices][BLE adverts][rejected cmds][incoming]
        uint8_t counts[10] = {bt_debug_heartbeat, bt_debug_discovered, bt_debug_connected, bt_debug_ready, bt_debug_reports,
                              bt_debug_inq_done, bt_debug_inq_result, bt_debug_adv, bt_debug_cmd_err, bt_debug_conn_req};
        uint8_t stage = bt_debug_stage;
        uint16_t *d = (uint16_t *) dest;
        for (int b = 0; b < 11; b++) {
            uint16_t c = b == 0 ? stage_col[stage < 6 ? stage : 0]
                                : (counts[b - 1] ? cyc[(counts[b - 1] - 1u) & 3u] : 0x2104);
            if (b == 4 && bt_debug_ready && bt_debug_dpad_only) c = 0x07E0; // green: d-pad only layout
            uint16_t *p = d + b * 8 + (b >= 6 ? 8 : 0);
            for (int i = 0; i < 6; i++) p[i] = c;
            p[6] = p[7] = 0;
        }
        // then what the controller is sending right now (white = yes): [left X][left Y][hat d-pad][right X]
        uint8_t raw = bt_debug_raw;
        for (int b = 0; b < 4; b++) {
            uint16_t c = (raw >> b) & 1u ? 0xFFFF : 0x2104;
            uint16_t *p = d + (11 + b) * 8 + 16;
            for (int i = 0; i < 6; i++) p[i] = c;
            p[6] = p[7] = 0;
        }
    }
#endif
}

static void __not_in_flash_func(lcd_fill_irq_handler)(void) {
#if USE_INTERP
    need_save = interp_in_use;
    interp_updated = 0;
#endif
#if PICO_BUTTON_INPUT
    // cheap (rate limited to every 2ms); this interrupt runs continuously, so it's a steady sample clock
    I_ButtonsSample();
#endif
    while (lcd_queue_count < LCD_NUM_LINE_BUFFERS) {
        if (!lcd_in_frame) {
            // the RAMWR which restarts the frame can't be sent until the previous frame has fully left
            // the DMA; the DMA IRQ re-pends us when it goes idle
            if (lcd_dma_active) break;
            lcd_begin_frame();
            lcd_frame_number++;
            new_frame_stuff();
            lcd_in_frame = true;
            lcd_src_line = 0;
            lcd_line_mod5 = 0;
        }
        uint8_t idx = lcd_queue_tail;
        lcd_render_scanline(lcd_line_buffers[idx], lcd_src_line);
#if LCD_ASPECT_CORRECT
        // 200 -> 240 rows: every 5th source line is sent twice
        lcd_line_repeats[idx] = lcd_line_mod5 == 4 ? 2 : 1;
        lcd_line_mod5 = lcd_line_mod5 == 4 ? 0 : lcd_line_mod5 + 1;
#else
        lcd_line_repeats[idx] = 1;
#endif
        lcd_queue_tail = idx == LCD_NUM_LINE_BUFFERS - 1 ? 0 : idx + 1;
        uint32_t save = save_and_disable_interrupts();
        lcd_queue_count++;
        if (!lcd_dma_active) lcd_start_head();
        restore_interrupts(save);
        if (++lcd_src_line == SCREENHEIGHT) lcd_in_frame = false;
    }
#if USE_INTERP
    if (interp_updated && need_save) {
        interp_restore_static(interp0, &interp0_save);
        interp_restore_static(interp1, &interp1_save);
    }
#endif
}
#pragma GCC pop_options

static void core1() {
#if PICO_BLUETOOTH_INPUT
    // BTstack (core 0) stores pairing keys with flash_safe_execute, which needs core 1 to park itself in RAM
    multicore_lockout_victim_init();
#endif
    lcd_init(LCD_Y0, LCD_ROWS);
    // DMA completion must preempt line generation so the panel is kept fed
    irq_set_exclusive_handler(DMA_IRQ_0, lcd_dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, 0x40);
    irq_set_enabled(DMA_IRQ_0, true);
    irq_set_exclusive_handler(LCD_FILL_IRQ, lcd_fill_irq_handler);
    irq_set_priority(LCD_FILL_IRQ, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(LCD_FILL_IRQ, true);
    lcd_pend_fill();
    sem_release(&core1_launch);
    while (true) {
        pd_core1_loop();
        tight_loop_contents();
    }
}

#if PICO_RP2350
#include "hardware/structs/accessctrl.h"
#endif
void I_InitGraphics(void)
{
    stbar = resolve_vpatch_handle(VPATCH_STBAR);
    sem_init(&render_frame_ready, 0, 2);
    sem_init(&display_frame_freed, 1, 2);
    sem_init(&core1_launch, 0, 1);
    pd_init();
    multicore_launch_core1(core1);
    // wait for core1 launch as it may do malloc and we have no mutex around that
    sem_acquire_blocking(&core1_launch);
#if USE_ZONE_FOR_MALLOC
    disallow_core1_malloc = true;
#endif
#if PICO_RP2350
    hw_set_bits(&accessctrl_hw->xip_ctrl, ACCESSCTRL_PASSWORD_BITS | 0xff);
#endif
    initialized = true;
}

// Bind all variables controlling video options into the configuration
// file system.
void I_BindVideoVariables(void)
{
//    M_BindIntVariable("use_mouse",                 &usemouse);
//    M_BindIntVariable("fullscreen",                &fullscreen);
//    M_BindIntVariable("video_display",             &video_display);
//    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
//    M_BindIntVariable("integer_scaling",           &integer_scaling);
//    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
//    M_BindIntVariable("startup_delay",             &startup_delay);
//    M_BindIntVariable("fullscreen_width",          &fullscreen_width);
//    M_BindIntVariable("fullscreen_height",         &fullscreen_height);
//    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
//    M_BindIntVariable("max_scaling_buffer_pixels", &max_scaling_buffer_pixels);
//    M_BindIntVariable("window_width",              &window_width);
//    M_BindIntVariable("window_height",             &window_height);
//    M_BindIntVariable("grabmouse",                 &grabmouse);
//    M_BindStringVariable("video_driver",           &video_driver);
//    M_BindStringVariable("window_position",        &window_position);
//    M_BindIntVariable("usegamma",                  &usegamma);
//    M_BindIntVariable("png_screenshots",           &png_screenshots);
}

//
// I_StartTic
//
void I_StartTic (void)
{
    if (!initialized)
    {
        return;
    }

    I_GetEvent();
//
//    if (usemouse && !nomouse && window_focused)
//    {
//        I_ReadMouse();
//    }
//
//    if (joywait < I_GetTime())
//    {
//        I_UpdateJoystick();
//    }
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

int I_GetPaletteIndex(int r, int g, int b)
{
    return 0;
}

#if !NO_USE_ENDDOOM
void I_Endoom(byte *endoom_data) {
#if SUPPORT_TEXT
    uint32_t size;
    uint8_t *wa = pd_get_work_area(&size);
    assert(size >=TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 + 80*25*2 + 4096);
    text_screen_cpy = wa;
    text_font_cpy = text_screen_cpy + 80 * 25 * 2;
    text_scanline_buffer_start = (uint32_t *) (text_font_cpy + 4096);
#if 0
    static_assert(sizeof(normal_font_data) == 4096, "");
    memcpy(text_font_cpy, normal_font_data, sizeof(normal_font_data));
    memcpy(text_screen_cpy, endoom_data, 80 * 25 * 2);
#else
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 1024 + 512, "");
    uint8_t *tmp_buf = (uint8_t *)text_scanline_buffer_start;
    uint16_t *decoder = (uint16_t *)(tmp_buf + 512);
    th_bit_input bi;
    th_bit_input_init(&bi, normal_font_data_z);
    decode_data(text_font_cpy, 4096, &bi, decoder, 512, tmp_buf, 512);
    th_bit_input_init(&bi, endoom_data);
    // text
    decode_data(text_screen_cpy, 80*25, &bi, decoder, 512, tmp_buf, 512);
    // attr
    decode_data(text_screen_cpy+80*25, 80*25, &bi, decoder, 512, tmp_buf, 512);
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 80*25*2, "");
    // re-interlace the text & attr
    memcpy(tmp_buf, text_screen_cpy, 80*25*2);
    for(int i=0;i<80*25;i++) {
        text_screen_cpy[i*2] = tmp_buf[i];
        text_screen_cpy[i*2+1] = tmp_buf[80*25 + i];
    }
#endif
    text_screen_data = text_screen_cpy;
#endif
}
#endif

void I_GraphicsCheckCommandLine(void)
{
//    int i;
//
//    //!
//    // @category video
//    // @vanilla
//    //
//    // Disable blitting the screen.
//    //
//
//    noblit = M_CheckParm ("-noblit");
//
//    //!
//    // @category video
//    //
//    // Don't grab the mouse when running in windowed mode.
//    //
//
//    nograbmouse_override = M_ParmExists("-nograbmouse");
//
//    // default to fullscreen mode, allow override with command line
//    // nofullscreen because we love prboom
//
//    //!
//    // @category video
//    //
//    // Run in a window.
//    //
//
//    if (M_CheckParm("-window") || M_CheckParm("-nofullscreen"))
//    {
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    //
//    // Run in fullscreen mode.
//    //
//
//    if (M_CheckParm("-fullscreen"))
//    {
//        fullscreen = true;
//    }
//
//    //!
//    // @category video
//    //
//    // Disable the mouse.
//    //
//
//    nomouse = M_CheckParm("-nomouse") > 0;
//
//    //!
//    // @category video
//    // @arg <x>
//    //
//    // Specify the screen width, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-width", 1);
//
//    if (i > 0)
//    {
//        window_width = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <y>
//    //
//    // Specify the screen height, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-height", 1);
//
//    if (i > 0)
//    {
//        window_height = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <WxY>
//    //
//    // Specify the dimensions of the window. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-geometry", 1);
//
//    if (i > 0)
//    {
//        int w, h, s;
//
//        s = sscanf(myargv[i + 1], "%ix%i", &w, &h);
//        if (s == 2)
//        {
//            window_width = w;
//            window_height = h;
//            fullscreen = false;
//        }
//    }
//
//    //!
//    // @category video
//    //
//    // Don't scale up the screen. Implies -window.
//    //
//
//    if (M_CheckParm("-1"))
//    {
//        SetScaleFactor(1);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 2x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-2"))
//    {
//        SetScaleFactor(2);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 3x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-3"))
//    {
//        SetScaleFactor(3);
//    }
}

// Check if we have been invoked as a screensaver by xscreensaver.

void I_CheckIsScreensaver(void)
{
}

void I_DisplayFPSDots(boolean dots_on)
{
}


#endif // PICODOOM_RENDER_NEWHOPE
