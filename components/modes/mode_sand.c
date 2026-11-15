/*
 * mode_sand.c — "pixel gravity": a falling-sand world on the round glass.
 *
 * A 155x155 cell grid drawn at 3px per cell. Gravity comes from the
 * accelerometer, so the whole world slides and the water sloshes when you
 * tilt the board. Touch paints material. Water soaks into soil, and plants
 * grow out of soil that is wet.
 *
 * No UI: the only affordance is a thin rim flash in the current material's
 * colour when you press ACTION to change what you are painting.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sand";

/* Cell size 4 (not 3): a coarser grid is 44% fewer cells to simulate, and a
 * 4px run is two aligned 32-bit stores per row when drawing. */
#define CELL   4
#define GW     116
#define GH     116
#define ORIGIN 0                       /* 116*4 = 464; keep 0 so each cell row
                                        * starts 4-byte aligned for the 32-bit
                                        * stores in render() */

/* Water is a closed system: it moves and soaks away but is never created
 * except by the generator and your finger, and only up to this many cells. */
#define WATER_CAP 5200

enum {
    EMPTY = 0, WALL, SAND, WATER, STONE, SOIL, WET_SOIL, PLANT, MAT_COUNT
};

/* What ACTION cycles through. */
static const uint8_t PAINTS[] = { SAND, WATER, SOIL, STONE, PLANT, EMPTY };
#define PAINT_COUNT (sizeof(PAINTS) / sizeof(PAINTS[0]))

static uint8_t *s_cell;                /* GW*GH */
static uint8_t *s_prev;                /* last drawn frame, for dirty redraw */
static uint8_t *s_moved;               /* GW*GH, cleared each frame */
static int      s_paint;               /* index into PAINTS */
static uint32_t s_hint_ms;             /* rim-flash countdown */
static bool     s_full_redraw;         /* force a complete repaint */
static int      s_water;               /* cells of water currently in play */
static uint32_t s_recount_ms;

static inline uint8_t at(int x, int y)
{
    if ((unsigned)x >= GW || (unsigned)y >= GH) return WALL;
    return s_cell[y * GW + x];
}
static inline void put(int x, int y, uint8_t v)
{
    if ((unsigned)x >= GW || (unsigned)y >= GH) return;
    s_cell[y * GW + x] = v;
}
static inline bool moved(int x, int y)
{
    return (unsigned)x < GW && (unsigned)y < GH && s_moved[y * GW + x];
}
static inline void mark(int x, int y)
{
    if ((unsigned)x < GW && (unsigned)y < GH) s_moved[y * GW + x] = 1;
}

/* Cells whose centre falls outside the round glass are permanent walls, so
 * material piles up against the rim instead of vanishing into the corners. */
static bool cell_on_glass(int x, int y)
{
    return fx_on_glass(ORIGIN + x * CELL + CELL / 2, ORIGIN + y * CELL + CELL / 2);
}

/* ------------------------------------------------------------------ */
/*  World generation                                                   */
/* ------------------------------------------------------------------ */

static void generate(void)
{
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            put(x, y, cell_on_glass(x, y) ? EMPTY : WALL);

    /* A rolling landscape: smooth sine bed plus a drifting random walk. */
    float drift = 0.0f;
    int   base  = (int)(GH * 0.62f);
    for (int x = 0; x < GW; x++) {
        drift += (float)fx_rand_range(-50, 50) * 0.02f;
        if (drift >  14.0f) drift =  14.0f;
        if (drift < -14.0f) drift = -14.0f;
        int h = base + (int)(sinf(x * 0.055f) * 11.0f + drift);

        for (int y = h; y < GH; y++) {
            if (at(x, y) == WALL) continue;
            int depth = y - h;
            put(x, y, depth < 6 ? SOIL : STONE);
        }
    }

    /* Pour a little water into the left and right dips so there is something
     * to slosh immediately. Sized well under WATER_CAP. */
    for (int i = 0; i < 2; i++) {
        int cx = (i == 0) ? GW / 4 : (GW * 3) / 4;
        for (int y = base - 16; y < base; y++)
            for (int x = cx - 12; x < cx + 12; x++)
                if (at(x, y) == EMPTY) put(x, y, WATER);
    }

    /* A few seeds on the surface; they will sprout once the soil wets. */
    for (int i = 0; i < 14; i++) {
        int x = fx_rand_range(4, GW - 4);
        for (int y = 2; y < GH - 1; y++) {
            if (at(x, y) == SOIL) { put(x, y - 1, PLANT); break; }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Simulation                                                         */
/* ------------------------------------------------------------------ */

static inline bool displaceable(uint8_t v)      { return v == EMPTY || v == WATER; }

/* Returns true if the cell actually moved. Callers must check this: the
 * `moved` flag marks the destination, not the source, so testing moved(x,y)
 * after a successful move reports false and lets follow-up code act on a cell
 * that is no longer there. */
static bool step_powder(int x, int y, uint8_t self, int dx, int dy, int px, int py)
{
    /* Straight down-gravity first; sand sinks through water. */
    uint8_t below = at(x + dx, y + dy);
    if (displaceable(below) && !(self == WATER && below == WATER)) {
        put(x + dx, y + dy, self);
        put(x, y, below == WATER ? WATER : EMPTY);
        mark(x + dx, y + dy);
        return true;
    }

    /* Then the two down-diagonals, in random order so piles stay symmetric. */
    int first = (fx_rand() & 1) ? 1 : -1;
    for (int i = 0; i < 2; i++) {
        int s  = first * (i == 0 ? 1 : -1);
        int nx = x + dx + px * s, ny = y + dy + py * s;
        if (at(nx, ny) == EMPTY) {
            put(nx, ny, self);
            put(x, y, EMPTY);
            mark(nx, ny);
            return true;
        }
    }
    return false;
}

static void step_water(int x, int y, int dx, int dy, int px, int py)
{
    /* Soak into neighbouring soil now and then — this is what lets plants
     * grow, and it stops the world turning into an ocean. */
    if ((fx_rand() & 0x3F) == 0) {
        const int nx[4] = { x + 1, x - 1, x, x };
        const int ny[4] = { y, y, y + 1, y - 1 };
        for (int i = 0; i < 4; i++) {
            if (at(nx[i], ny[i]) == SOIL) {
                put(nx[i], ny[i], WET_SOIL);
                put(x, y, EMPTY);
                s_water--;                    /* water leaves the system here */
                return;
            }
        }
    }

    if (step_powder(x, y, WATER, dx, dy, px, py)) return;

    /* Still stuck: spread sideways so it levels out. This is a move, not a
     * spawn — bail out if the cell no longer holds water. */
    if (at(x, y) != WATER) return;
    int s = (fx_rand() & 1) ? 1 : -1;
    for (int i = 0; i < 2; i++, s = -s) {
        int nx = x + px * s, ny = y + py * s;
        if (at(nx, ny) == EMPTY) {
            put(nx, ny, WATER);
            put(x, y, EMPTY);
            mark(nx, ny);
            return;
        }
    }
}

static void step_plant(int x, int y, int dx, int dy, int px, int py)
{
    if ((fx_rand() & 0x7F) != 0) return;           /* grow slowly */

    /* Only grow when rooted in something that can feed it. */
    uint8_t root = at(x + dx, y + dy);
    if (root != WET_SOIL && root != PLANT) return;

    int ux = x - dx, uy = y - dy;                  /* against gravity */
    if ((fx_rand() % 3) == 0) {                    /* occasional lean */
        int s = (fx_rand() & 1) ? 1 : -1;
        ux += px * s;
        uy += py * s;
    }
    if (at(ux, uy) == EMPTY) put(ux, uy, PLANT);
}

static void simulate(void)
{
    float gx, gy;
    bsp_imu_gravity(&gx, &gy);

    int dx = (gx > 0.40f) - (gx < -0.40f);
    int dy = (gy > 0.40f) - (gy < -0.40f);
    if (dx == 0 && dy == 0) dy = 1;
    int px = -dy, py = dx;                         /* perpendicular */

    memset(s_moved, 0, GW * GH);

    /* Sweep from the down-gravity side so falling material cascades in one
     * pass instead of smearing. */
    int y0 = (dy > 0) ? GH - 1 : 0, y1 = (dy > 0) ? -1 : GH, ys = (dy > 0) ? -1 : 1;
    int x0 = (dx > 0) ? GW - 1 : 0, x1 = (dx > 0) ? -1 : GW, xs = (dx > 0) ? -1 : 1;

    for (int y = y0; y != y1; y += ys) {
        for (int x = x0; x != x1; x += xs) {
            if (moved(x, y)) continue;
            switch (at(x, y)) {
            case SAND:
            case SOIL:
            case WET_SOIL: step_powder(x, y, at(x, y), dx, dy, px, py); break;
            case WATER:    step_water(x, y, dx, dy, px, py);            break;
            case PLANT:    step_plant(x, y, dx, dy, px, py);            break;
            default: break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Painting                                                           */
/* ------------------------------------------------------------------ */

/* Count the water actually on the board. Cheap enough to run occasionally,
 * and it means no amount of bookkeeping drift can let the world flood. */
static int count_water(void)
{
    int n = 0;
    for (int i = 0; i < GW * GH; i++) if (s_cell[i] == WATER) n++;
    return n;
}

static void paint_at(int sx, int sy)
{
    int cx = (sx - ORIGIN) / CELL, cy = (sy - ORIGIN) / CELL;
    uint8_t mat = PAINTS[s_paint];
    const int r = 3;

    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (at(x, y) == WALL) continue;
            /* Loose material only lands in free space; solids and the eraser
             * overwrite whatever is there. */
            if (mat == SAND || mat == WATER || mat == PLANT) {
                if (at(x, y) != EMPTY) continue;
            }
            if (mat == WATER) {
                if (s_water >= WATER_CAP) continue;   /* the tap runs dry */
                s_water++;
            }
            if (at(x, y) == WATER) s_water--;         /* painted over */
            put(x, y, mat);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Rendering                                                          */
/* ------------------------------------------------------------------ */

static uint16_t s_pal[MAT_COUNT][4];   /* 4 shades each, for texture */

static void build_palette(void)
{
    static const uint8_t base[MAT_COUNT][3] = {
        {   6,   7,  12 },   /* EMPTY    */
        {   0,   0,   0 },   /* WALL     */
        { 214, 178,  96 },   /* SAND     */
        {  46, 108, 196 },   /* WATER    */
        {  96,  98, 110 },   /* STONE    */
        { 108,  76,  50 },   /* SOIL     */
        {  68,  46,  30 },   /* WET_SOIL */
        {  74, 176,  86 },   /* PLANT    */
    };
    for (int m = 0; m < MAT_COUNT; m++) {
        for (int s = 0; s < 4; s++) {
            int d = (s - 1) * 7;       /* -7 .. +14 */
            int r = base[m][0] + d, g = base[m][1] + d, b = base[m][2] + d;
            s_pal[m][s] = fx_rgb((uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r),
                                 (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g),
                                 (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b));
        }
    }
}

/* Only repaint cells that changed since the last frame. The framebuffer
 * persists between frames, so a settled world costs almost nothing to draw —
 * this, not the grid size, is what sets the frame rate. */
static void render(void)
{
    uint16_t *fb = bsp_fb();
    bool all = s_full_redraw;
    s_full_redraw = false;

    for (int y = 0; y < GH; y++) {
        int row = y * GW;
        for (int x = 0; x < GW; x++) {
            uint8_t m = s_cell[row + x];
            if (!all && m == s_prev[row + x]) continue;
            s_prev[row + x] = m;

            /* Deterministic per-cell shade so the texture does not crawl. */
            uint16_t c = s_pal[m][(x * 7 + y * 13) & 3];
            uint32_t pair = ((uint32_t)c << 16) | c;

            int fy  = ORIGIN + y * CELL;
            int fx0 = ORIGIN + x * CELL;
            for (int r = 0; r < CELL; r++) {
                /* CELL is 4, so each row is two aligned 32-bit stores. */
                uint32_t *p = (uint32_t *)&fb[(fy + r) * BSP_FB_W + fx0];
                p[0] = pair;
                p[1] = pair;
            }
        }
    }

    /* Rim flash: the only feedback that the paint material changed. */
    if (s_hint_ms > 0) {
        fx_ring(fb, BSP_FB_W / 2, BSP_FB_H / 2, BSP_FB_W / 2 - 1, 4,
                s_pal[PAINTS[s_paint]][2]);
    }
}

/* ------------------------------------------------------------------ */
/*  Mode interface                                                     */
/* ------------------------------------------------------------------ */

/* Grids prefer internal RAM for speed, but PSRAM beats not running at all. */
static uint8_t *grid_alloc(void)
{
    uint8_t *p = heap_caps_malloc(GW * GH, MALLOC_CAP_INTERNAL);
    if (!p) {
        p = heap_caps_malloc(GW * GH, MALLOC_CAP_SPIRAM);
        if (p) ESP_LOGW(TAG, "grid fell back to PSRAM (internal RAM is tight)");
    }
    return p;
}

static void sand_enter(void)
{
    s_cell  = grid_alloc();
    s_prev  = grid_alloc();
    s_moved = grid_alloc();
    if (!s_cell || !s_prev || !s_moved) {
        ESP_LOGE(TAG, "out of memory for the grid (%d bytes x3)", GW * GH);
        return;
    }
    build_palette();
    generate();
    s_water       = count_water();
    s_paint       = 0;
    s_hint_ms     = 700;
    s_full_redraw = true;
    ESP_LOGI(TAG, "world seeded with %d water cells (cap %d)", s_water, WATER_CAP);
}

static void sand_leave(void)
{
    free(s_cell);  s_cell  = NULL;
    free(s_prev);  s_prev  = NULL;
    free(s_moved); s_moved = NULL;
}

static void sand_tick(uint32_t dt)
{
    if (!s_cell) {
        /* Never leave the screen black and silent: a mode that could not
         * start says so in a colour you cannot mistake for "off". */
        fx_clear(bsp_fb(), fx_rgb(120, 0, 0));
        return;
    }

    uint16_t tx, ty;
    if (bsp_touch_get_point(&tx, &ty)) paint_at(tx, ty);

    simulate();

    /* Recount periodically: cheap, and it makes the cap impossible to defeat
     * through any bookkeeping slip elsewhere in the sim. */
    s_recount_ms += dt;
    if (s_recount_ms >= 2000) {
        s_recount_ms = 0;
        s_water = count_water();
    }

    bool hint_was = s_hint_ms > 0;
    s_hint_ms = (s_hint_ms > dt) ? s_hint_ms - dt : 0;
    if (hint_was && s_hint_ms == 0) s_full_redraw = true;   /* clear the rim */

    render();
}

static void sand_action(void)
{
    s_paint   = (s_paint + 1) % PAINT_COUNT;
    s_hint_ms = 700;
    ESP_LOGI(TAG, "paint -> %d", PAINTS[s_paint]);
}

const chaos_mode_t mode_sand = {
    .name = "pixel gravity",
    .enter = sand_enter, .leave = sand_leave,
    .tick = sand_tick,   .action = sand_action,
};
