/*
 * mode_rpg.c — a procedurally generated world you leave voice notes in.
 *
 * Value-noise terrain (water / sand / grass / forest / rock) on a 16px tile
 * grid, larger than the screen, with the camera following the player. Drag
 * anywhere and the player walks toward your finger.
 *
 * Shrines mark where notes were left. ACTION drops a shrine where you stand;
 * stand next to one and ACTION plays it back instead.
 *
 * STATUS: the world, movement and shrines are real. Audio capture/playback
 * is NOT wired up yet — the ES7210 mic ADC (0x40) and ES8311 codec (0x18) are
 * both present on the I2C bus, and recording to the 24MB FAT partition is the
 * next increment. Until then a shrine stores its position and a placeholder.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"

#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "rpg";

#define TILE      16
#define WORLD_W   96                    /* tiles */
#define WORLD_H   96
#define WORLD_PX_W (WORLD_W * TILE)
#define WORLD_PX_H (WORLD_H * TILE)
#define MAX_SHRINES 24
#define NEAR_DIST   44                  /* px, "you can hear it from here" */

enum { T_WATER = 0, T_SAND, T_GRASS, T_FOREST, T_ROCK, T_COUNT };

typedef struct { int x, y; bool has_note; } shrine_t;

static uint8_t  s_tile[WORLD_H][WORLD_W];
static shrine_t s_shrine[MAX_SHRINES];
static int      s_shrine_count;
static float    s_px, s_py;             /* player, world pixels */
static uint32_t s_pulse_ms;
static int      s_near = -1;            /* index of shrine in range, or -1 */
static uint16_t s_pal[T_COUNT][3];

/* ------------------------------------------------------------------ */
/*  Terrain                                                            */
/* ------------------------------------------------------------------ */

static float hash2(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

static float vnoise(float x, float y, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);

    float a = hash2(xi,     yi,     seed), b = hash2(xi + 1, yi,     seed);
    float c = hash2(xi,     yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

static float fbm(float x, float y, uint32_t seed)
{
    return vnoise(x, y, seed) * 0.55f
         + vnoise(x * 2.1f, y * 2.1f, seed ^ 0x9E37) * 0.30f
         + vnoise(x * 4.3f, y * 4.3f, seed ^ 0x51ED) * 0.15f;
}

static void generate(uint32_t seed)
{
    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            float e = fbm(x * 0.07f, y * 0.07f, seed);
            /* Pull the edges down so the map is an island, not a hard cut. */
            float nx = (float)x / WORLD_W - 0.5f, ny = (float)y / WORLD_H - 0.5f;
            e -= (nx * nx + ny * ny) * 1.15f;

            uint8_t t = T_ROCK;
            if      (e < 0.10f) t = T_WATER;
            else if (e < 0.16f) t = T_SAND;
            else if (e < 0.34f) t = T_GRASS;
            else if (e < 0.46f) t = T_FOREST;
            s_tile[y][x] = t;
        }
    }
}

static bool walkable(int wx, int wy)
{
    int tx = wx / TILE, ty = wy / TILE;
    if ((unsigned)tx >= WORLD_W || (unsigned)ty >= WORLD_H) return false;
    return s_tile[ty][tx] != T_WATER;
}

/* ------------------------------------------------------------------ */
/*  Rendering                                                          */
/* ------------------------------------------------------------------ */

static void build_palette(void)
{
    static const uint8_t base[T_COUNT][3] = {
        {  28,  58, 110 },   /* water  */
        { 198, 176, 118 },   /* sand   */
        {  74, 132,  66 },   /* grass  */
        {  38,  86,  50 },   /* forest */
        { 110, 108, 104 },   /* rock   */
    };
    for (int t = 0; t < T_COUNT; t++)
        for (int s = 0; s < 3; s++) {
            int d = (s - 1) * 9;
            int r = base[t][0] + d, g = base[t][1] + d, b = base[t][2] + d;
            s_pal[t][s] = fx_rgb((uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r),
                                 (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g),
                                 (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b));
        }
}

static void render(void)
{
    uint16_t *fb = bsp_fb();

    /* Camera centred on the player, clamped to the world. */
    int camx = (int)s_px - BSP_FB_W / 2;
    int camy = (int)s_py - BSP_FB_H / 2;
    if (camx < 0) camx = 0;
    if (camy < 0) camy = 0;
    if (camx > WORLD_PX_W - BSP_FB_W) camx = WORLD_PX_W - BSP_FB_W;
    if (camy > WORLD_PX_H - BSP_FB_H) camy = WORLD_PX_H - BSP_FB_H;

    int t0x = camx / TILE, t0y = camy / TILE;
    for (int ty = t0y; ty <= t0y + BSP_FB_H / TILE + 1 && ty < WORLD_H; ty++) {
        for (int tx = t0x; tx <= t0x + BSP_FB_W / TILE + 1 && tx < WORLD_W; tx++) {
            int sx = tx * TILE - camx, sy = ty * TILE - camy;
            uint16_t c = s_pal[s_tile[ty][tx]][(tx * 5 + ty * 3) % 3];
            fx_rect(fb, sx, sy, TILE, TILE, c);
        }
    }

    /* Shrines: a stone with a glow, brighter when it holds a note. */
    for (int i = 0; i < s_shrine_count; i++) {
        int sx = s_shrine[i].x - camx, sy = s_shrine[i].y - camy;
        if (sx < -20 || sy < -20 || sx > BSP_FB_W + 20 || sy > BSP_FB_H + 20) continue;

        fx_disc(fb, sx, sy, 7, fx_rgb(30, 28, 36));
        fx_disc(fb, sx, sy, 5, s_shrine[i].has_note ? fx_rgb(230, 200, 110)
                                                    : fx_rgb(90, 90, 100));
        if (i == s_near) {
            /* In range: a pulsing ring is the whole "prompt" — no HUD. */
            float ph = (float)s_pulse_ms / 900.0f * 6.28318f;
            int   r  = 12 + (int)(4.0f * sinf(ph));
            fx_ring(fb, sx, sy, r, 2, fx_rgb(250, 230, 150));
        }
    }

    /* Player. */
    int psx = (int)s_px - camx, psy = (int)s_py - camy;
    fx_disc(fb, psx, psy + 1, 7, fx_rgb(10, 10, 14));
    fx_disc(fb, psx, psy, 6, fx_rgb(240, 240, 250));
    fx_disc(fb, psx, psy - 1, 3, fx_rgb(40, 40, 60));
}

/* ------------------------------------------------------------------ */
/*  Mode interface                                                     */
/* ------------------------------------------------------------------ */

static void rpg_enter(void)
{
    build_palette();
    generate(fx_rand());

    /* Drop the player on the first walkable tile near the middle. */
    s_px = WORLD_PX_W / 2.0f;
    s_py = WORLD_PX_H / 2.0f;
    for (int r = 0; r < 40 && !walkable((int)s_px, (int)s_py); r++) {
        s_px += TILE;
        if (s_px > WORLD_PX_W - TILE) { s_px = TILE; s_py += TILE; }
    }

    s_shrine_count = 0;
    s_near = -1;
}

static void rpg_leave(void) { }

static void rpg_tick(uint32_t dt)
{
    s_pulse_ms = (s_pulse_ms + dt) % 900;

    /* Walk toward the finger. */
    uint16_t tx, ty;
    if (bsp_touch_get_point(&tx, &ty)) {
        float dx = (float)tx - BSP_FB_W / 2.0f;
        float dy = (float)ty - BSP_FB_H / 2.0f;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 12.0f) {
            float speed = 0.085f * (float)dt;          /* px per ms */
            if (speed > 6.0f) speed = 6.0f;
            float nx = s_px + dx / len * speed;
            float ny = s_py + dy / len * speed;
            /* Axis-separated so you slide along a shoreline instead of sticking. */
            if (walkable((int)nx, (int)s_py)) s_px = nx;
            if (walkable((int)s_px, (int)ny)) s_py = ny;
        }
    }

    /* Which shrine, if any, is within earshot. */
    s_near = -1;
    for (int i = 0; i < s_shrine_count; i++) {
        float dx = s_shrine[i].x - s_px, dy = s_shrine[i].y - s_py;
        if (dx * dx + dy * dy < (float)(NEAR_DIST * NEAR_DIST)) { s_near = i; break; }
    }

    render();
}

static void rpg_action(void)
{
    if (s_near >= 0) {
        /* TODO: play back the note stored for this shrine. */
        ESP_LOGI(TAG, "play shrine %d (audio not implemented yet)", s_near);
        return;
    }
    if (s_shrine_count >= MAX_SHRINES) {
        ESP_LOGW(TAG, "shrine limit reached");
        return;
    }
    /* TODO: record from the ES7210 mic array into the FAT partition. */
    s_shrine[s_shrine_count].x = (int)s_px;
    s_shrine[s_shrine_count].y = (int)s_py;
    s_shrine[s_shrine_count].has_note = true;
    ESP_LOGI(TAG, "shrine %d placed at %d,%d (audio not implemented yet)",
             s_shrine_count, (int)s_px, (int)s_py);
    s_shrine_count++;
}

const chaos_mode_t mode_rpg = {
    .name = "voice-note rpg",
    .enter = rpg_enter, .leave = rpg_leave,
    .tick = rpg_tick,   .action = rpg_action,
};
