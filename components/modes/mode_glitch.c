/*
 * mode_glitch.c — procedural sigils put through a databending pass.
 *
 * A sigil is generated from a seed (nodes on a circle wired into a star
 * polygon, plus rings and tick marks), then the framebuffer itself is
 * mangled: rows slip sideways, colour channels separate, blocks get copied
 * over each other, and previous frames smear when trails are on.
 *
 * Dragging a finger cycles sigils fast — X sets glitch intensity, Y sets
 * complexity. ACTION rerolls everything.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "glitch";

#define CX (BSP_FB_W / 2)
#define CY (BSP_FB_H / 2)

enum {
    FX_ROWSLIP  = 1 << 0,
    FX_CHROMA   = 1 << 1,
    FX_BLOCKS   = 1 << 2,
    FX_NOISE    = 1 << 3,
    FX_TRAILS   = 1 << 4,
};

static uint32_t  s_seed;
static int       s_nodes, s_skip;
static float     s_intensity;
static uint8_t   s_effects;
static uint16_t  s_ink, s_accent;
static uint32_t  s_morph_ms;
static uint32_t  s_frame;
static uint16_t *s_rowbuf;             /* scratch for row displacement */

/* ------------------------------------------------------------------ */
/*  Sigil                                                              */
/* ------------------------------------------------------------------ */

static void reroll(void)
{
    s_seed    = fx_rand();
    s_nodes   = fx_rand_range(5, 13);
    s_skip    = fx_rand_range(2, s_nodes - 1);
    s_effects = (uint8_t)(fx_rand() & 0x1F);
    if (!s_effects) s_effects = FX_ROWSLIP | FX_CHROMA;

    /* Harsh, high-contrast pairs — this should look like a broken monitor. */
    static const uint8_t inks[][3] = {
        { 255, 255, 255 }, { 120, 255, 180 }, { 255, 90, 160 },
        { 140, 170, 255 }, { 255, 210, 90 },
    };
    static const uint8_t accents[][3] = {
        { 255, 40, 90 }, { 40, 255, 220 }, { 190, 90, 255 },
        { 255, 160, 40 }, { 90, 255, 120 },
    };
    int i = (int)(fx_rand() % 5), j = (int)(fx_rand() % 5);
    s_ink    = fx_rgb(inks[i][0], inks[i][1], inks[i][2]);
    s_accent = fx_rgb(accents[j][0], accents[j][1], accents[j][2]);
}

static void thick_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    fx_line(fb, x0, y0, x1, y1, c);
    fx_line(fb, x0 + 1, y0, x1 + 1, y1, c);
    fx_line(fb, x0, y0 + 1, x1, y1 + 1, c);
}

static void draw_sigil(uint16_t *fb)
{
    const int R = 150 + (int)(s_intensity * 26.0f);

    /* Nodes on the circle. */
    int nx[16], ny[16];
    for (int i = 0; i < s_nodes; i++) {
        float a = (float)i / s_nodes * 6.28318f - 1.5708f;
        nx[i] = CX + (int)(cosf(a) * R);
        ny[i] = CY + (int)(sinf(a) * R);
    }

    /* Star polygon: every node wired to the one s_skip steps away. */
    for (int i = 0; i < s_nodes; i++) {
        int j = (i + s_skip) % s_nodes;
        thick_line(fb, nx[i], ny[i], nx[j], ny[j], s_ink);
    }

    /* Enclosing rings and node pips. */
    fx_ring(fb, CX, CY, R + 14, 2, s_accent);
    fx_ring(fb, CX, CY, R + 22, 1, s_ink);
    for (int i = 0; i < s_nodes; i++) fx_disc(fb, nx[i], ny[i], 4, s_accent);

    /* Tick marks around the rim, seeded so they are stable per sigil. */
    uint32_t save = s_seed;
    fx_srand(s_seed);
    int ticks = 12 + (int)(fx_rand() % 24);
    for (int i = 0; i < ticks; i++) {
        float a  = (float)i / ticks * 6.28318f;
        int   r0 = R + 26, r1 = r0 + 4 + (int)(fx_rand() % 10);
        fx_line(fb, CX + (int)(cosf(a) * r0), CY + (int)(sinf(a) * r0),
                    CX + (int)(cosf(a) * r1), CY + (int)(sinf(a) * r1), s_ink);
    }
    /* A couple of inner chords for asymmetry. */
    for (int i = 0; i < 3; i++) {
        int a = (int)(fx_rand() % (uint32_t)s_nodes);
        int b = (int)(fx_rand() % (uint32_t)s_nodes);
        if (a != b) thick_line(fb, nx[a], ny[a], nx[b], ny[b], s_accent);
    }
    fx_srand(save ^ fx_rand());
}

/* ------------------------------------------------------------------ */
/*  Databending passes                                                 */
/* ------------------------------------------------------------------ */

/* Slide horizontal bands sideways — the classic torn-signal look. */
static void pass_rowslip(uint16_t *fb)
{
    int bands = 2 + (int)(s_intensity * 10.0f);
    for (int b = 0; b < bands; b++) {
        int y0 = fx_rand_range(0, BSP_FB_H);
        int h  = fx_rand_range(3, 26);
        int sh = fx_rand_range(-60, 60) * (int)(1 + s_intensity * 2.0f);
        if (!sh) continue;

        for (int y = y0; y < y0 + h && y < BSP_FB_H; y++) {
            uint16_t *row = &fb[y * BSP_FB_W];
            for (int x = 0; x < BSP_FB_W; x++) {
                int src = x - sh;
                src = ((src % BSP_FB_W) + BSP_FB_W) % BSP_FB_W;
                s_rowbuf[x] = row[src];
            }
            for (int x = 0; x < BSP_FB_W; x++) row[x] = s_rowbuf[x];
        }
    }
}

/* Pull the red channel off sideways from the rest. */
static void pass_chroma(uint16_t *fb)
{
    int off = 2 + (int)(s_intensity * 14.0f);
    int y0  = fx_rand_range(0, BSP_FB_H - 40);
    int h   = fx_rand_range(20, 140);

    for (int y = y0; y < y0 + h && y < BSP_FB_H; y++) {
        uint16_t *row = &fb[y * BSP_FB_W];
        for (int x = BSP_FB_W - 1; x >= off; x--) {
            uint8_t r0, g0, b0, r1, g1, b1;
            fx_unrgb(row[x], &r0, &g0, &b0);
            fx_unrgb(row[x - off], &r1, &g1, &b1);
            row[x] = fx_rgb(r1, g0, b0);
        }
    }
}

/* Copy rectangles over each other: cheap, convincing datamosh. */
static void pass_blocks(uint16_t *fb)
{
    int count = 1 + (int)(s_intensity * 8.0f);
    for (int i = 0; i < count; i++) {
        int w  = fx_rand_range(20, 140), h = fx_rand_range(8, 70);
        int sx = fx_rand_range(0, BSP_FB_W - w), sy = fx_rand_range(0, BSP_FB_H - h);
        int dx = sx + fx_rand_range(-50, 50),    dy = sy + fx_rand_range(-30, 30);
        if (dx < 0 || dy < 0 || dx + w >= BSP_FB_W || dy + h >= BSP_FB_H) continue;

        for (int y = 0; y < h; y++) {
            uint16_t *s = &fb[(sy + y) * BSP_FB_W + sx];
            uint16_t *d = &fb[(dy + y) * BSP_FB_W + dx];
            for (int x = 0; x < w; x++) d[x] = s[x];
        }
    }
}

static void pass_noise(uint16_t *fb)
{
    int n = (int)(s_intensity * 2600.0f);
    for (int i = 0; i < n; i++) {
        int x = fx_rand_range(0, BSP_FB_W), y = fx_rand_range(0, BSP_FB_H);
        fb[y * BSP_FB_W + x] = (fx_rand() & 1) ? s_ink : s_accent;
    }
}

/* Halve every channel in place — RGB565's standard "fade" mask, applied
 * through the byte swap the panel expects. */
static void pass_fade(uint16_t *fb)
{
    for (int i = 0; i < BSP_FB_PX; i++) {
        uint16_t c = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        c = (uint16_t)((c >> 1) & 0x7BEF);
        fb[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}

/* ------------------------------------------------------------------ */
/*  Mode interface                                                     */
/* ------------------------------------------------------------------ */

static void glitch_enter(void)
{
    s_rowbuf = heap_caps_malloc(BSP_FB_W * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    s_intensity = 0.35f;
    reroll();
}

static void glitch_leave(void)
{
    free(s_rowbuf);
    s_rowbuf = NULL;
}

static void glitch_tick(uint32_t dt)
{
    if (!s_rowbuf) return;
    uint16_t *fb = bsp_fb();
    s_frame++;

    uint16_t tx, ty;
    if (bsp_touch_get_point(&tx, &ty)) {
        /* Dragging drives the whole thing: intensity across, complexity down. */
        s_intensity = (float)tx / BSP_FB_W;
        s_nodes     = 5 + (int)((float)ty / BSP_FB_H * 8.0f);
        if (s_skip >= s_nodes) s_skip = 2;

        s_morph_ms += dt;
        if (s_morph_ms > 70) {          /* cycle sigils rapidly while held */
            s_morph_ms = 0;
            s_seed = fx_rand();
            s_skip = fx_rand_range(2, s_nodes - 1);
        }
    }

    if (s_effects & FX_TRAILS) {
        if ((s_frame % 3) == 0) pass_fade(fb);
    } else {
        fx_clear(fb, fx_rgb(4, 4, 8));
    }

    draw_sigil(fb);

    if (s_effects & FX_ROWSLIP) pass_rowslip(fb);
    if (s_effects & FX_CHROMA)  pass_chroma(fb);
    if (s_effects & FX_BLOCKS)  pass_blocks(fb);
    if (s_effects & FX_NOISE)   pass_noise(fb);
}

static void glitch_action(void)
{
    reroll();
    ESP_LOGI(TAG, "reroll: nodes=%d skip=%d fx=0x%02X", s_nodes, s_skip, s_effects);
}

const chaos_mode_t mode_glitch = {
    .name = "glitch sigils",
    .enter = glitch_enter, .leave = glitch_leave,
    .tick = glitch_tick,   .action = glitch_action,
};
