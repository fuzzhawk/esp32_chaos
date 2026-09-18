/*
 * fx.h — minimal pixel drawing onto the raw framebuffer.
 *
 * Colour format note: the CO5300 is fed RGB565 with the two bytes swapped
 * (this is what LVGL's LV_COLOR_16_SWAP used to do for us). Always build
 * colours with fx_rgb(); a raw 0xRRGGBB or plain RGB565 literal will come
 * out wrong on the glass.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pack 8-bit RGB into the panel's byte-swapped RGB565. */
static inline uint16_t fx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

/* Unpack a panel pixel back to 8-bit channels (for glitch/channel work). */
static inline void fx_unrgb(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t c = (uint16_t)((px >> 8) | (px << 8));
    *r = (uint8_t)((c >> 8) & 0xF8);
    *g = (uint8_t)((c >> 3) & 0xFC);
    *b = (uint8_t)((c << 3) & 0xF8);
}

/* Fast, cheap PRNG — we call this hundreds of times a frame. */
uint32_t fx_rand(void);
void     fx_srand(uint32_t seed);
static inline int fx_rand_range(int lo, int hi)   /* [lo, hi) */
{
    return lo + (int)(fx_rand() % (uint32_t)(hi - lo));
}

void fx_clear(uint16_t *fb, uint16_t colour);
void fx_px(uint16_t *fb, int x, int y, uint16_t colour);
void fx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t colour);
void fx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t colour);
void fx_ring(uint16_t *fb, int cx, int cy, int r, int thickness, uint16_t colour);
void fx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t colour);

/* True when (x,y) is inside the round glass — the corners are not visible. */
static inline bool fx_on_glass(int x, int y)
{
    int dx = x - BSP_FB_W / 2, dy = y - BSP_FB_H / 2;
    int r  = BSP_FB_W / 2;
    return (dx * dx + dy * dy) <= (r * r);
}

#ifdef __cplusplus
}
#endif
