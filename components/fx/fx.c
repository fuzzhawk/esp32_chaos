#include "fx.h"

static uint32_t s_rng = 0x1F123BB5u;

uint32_t fx_rand(void)
{
    /* xorshift32 — plenty random for sand and glitches, and fast enough to
     * call per-pixel without showing up in the frame budget. */
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

void fx_srand(uint32_t seed)
{
    s_rng = seed ? seed : 0x1F123BB5u;
}

void fx_clear(uint16_t *fb, uint16_t colour)
{
    /* memset only works for uniform bytes; fill 32 bits at a time instead. */
    uint32_t pair = ((uint32_t)colour << 16) | colour;
    uint32_t *p = (uint32_t *)fb;
    for (int i = 0; i < BSP_FB_PX / 2; i++) p[i] = pair;
}

void fx_px(uint16_t *fb, int x, int y, uint16_t colour)
{
    if ((unsigned)x >= BSP_FB_W || (unsigned)y >= BSP_FB_H) return;
    fb[y * BSP_FB_W + x] = colour;
}

void fx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t colour)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > BSP_FB_W) w = BSP_FB_W - x;
    if (y + h > BSP_FB_H) h = BSP_FB_H - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        uint16_t *p = &fb[(y + row) * BSP_FB_W + x];
        for (int col = 0; col < w; col++) p[col] = colour;
    }
}

void fx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t colour)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= BSP_FB_H) continue;
        int span = (int)(0.5f + __builtin_sqrtf((float)(r * r - dy * dy)));
        int x0 = cx - span, x1 = cx + span;
        if (x0 < 0) x0 = 0;
        if (x1 >= BSP_FB_W) x1 = BSP_FB_W - 1;
        uint16_t *p = &fb[y * BSP_FB_W];
        for (int x = x0; x <= x1; x++) p[x] = colour;
    }
}

void fx_ring(uint16_t *fb, int cx, int cy, int r, int thickness, uint16_t colour)
{
    if (r <= 0 || thickness <= 0) return;
    int inner = r - thickness;
    if (inner < 0) inner = 0;
    int r2 = r * r, i2 = inner * inner;

    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= BSP_FB_H) continue;
        uint16_t *p = &fb[y * BSP_FB_W];
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if ((unsigned)x >= BSP_FB_W) continue;
            int d = dx * dx + dy * dy;
            if (d <= r2 && d >= i2) p[x] = colour;
        }
    }
}

void fx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t colour)
{
    int dx =  (x1 > x0 ? x1 - x0 : x0 - x1);
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        fx_px(fb, x0, y0, colour);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
