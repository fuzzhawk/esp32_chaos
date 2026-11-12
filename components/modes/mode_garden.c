/*
 * mode_garden.c — one garden that grows in real time.
 *
 * Ported from the feed's garden game (www/js/games/garden.js). Kept faithful
 * where it matters: the plant table and their growth hours, the animals and
 * what draws each of them in, the ruins hidden under the overgrowth, growth
 * that continues while the device is off, and flowers that seed themselves.
 *
 * Changed for this device: no HUD and no seed picker, because chaosOS has no
 * UI. Touch walks the gardener; ACTION is one context verb — clear the grass
 * you are standing in, water a plant beside you, feed an animal beside you,
 * or plant the next seed in your pocket. Visitors and the gardener are drawn
 * with the Crystal sprite engine's characters.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"
#include "crystal.h"

#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "garden";

#define T        16                 /* tile pixels                        */
#define GW       64                 /* tiles across                       */
#define GH       64
#define MID      32
#define WORLD_W  (GW * T)
#define WORLD_H  (GH * T)
#define HR_SEC   3600               /* one growth hour, in real seconds    */

#define MAX_PLANTS  200
#define MAX_ANIMALS 48
#define MAX_NPCS    4
#define MAX_RUINS   11

/* ground: 0 grass, 1 water, 2 shore sand. wild: 0 cleared .. 3 deep grass. */
#define TILE_WILD(v)    ((v) & 0x03)
#define TILE_GROUND(v)  (((v) >> 2) & 0x03)
#define TILE_MAKE(w, g) (uint8_t)(((w) & 3) | (((g) & 3) << 2))

enum { PT_FLOWER = 0, PT_SHRUB, PT_TREE };

typedef struct {
    const char *name;
    uint8_t  type;
    uint16_t hours;        /* real hours from seed to full grown */
    uint32_t main;         /* petal / leaf                       */
    uint32_t accent;       /* centre / fruit / blossom           */
    uint8_t  food;         /* bears fruit the animals eat        */
} plant_def_t;

/* The feed's PLANTS table, hours and colours preserved. */
static const plant_def_t PLANTS[] = {
    { "daisy",       PT_FLOWER,  2, 0xFFFFFF, 0xF6D35B, 0 },
    { "tulip",       PT_FLOWER,  3, 0xF59AB8, 0xFBD3E2, 0 },
    { "poppy",       PT_FLOWER,  3, 0xFF8A7A, 0x4A2F2F, 0 },
    { "lavender",    PT_FLOWER,  3, 0xB49BE8, 0xD8C8F4, 0 },
    { "bluebell",    PT_FLOWER,  3, 0x8FB2F0, 0xC0D4F8, 0 },
    { "cosmos",      PT_FLOWER,  3, 0xF6B3E0, 0xF3D27A, 0 },
    { "sunflower",   PT_FLOWER,  5, 0xFFD43B, 0x7A4B22, 0 },
    { "berrybush",   PT_SHRUB,   8, 0x5E9E55, 0xD6455E, 1 },
    { "rosebush",    PT_SHRUB,  10, 0x4F8F4A, 0xF28AA8, 0 },
    { "hydrangea",   PT_SHRUB,   9, 0x5A9A62, 0xA9BDF7, 0 },
    { "apple",       PT_TREE,   30, 0x6FB15F, 0xE2453F, 1 },
    { "cherry",      PT_TREE,   24, 0xF5B5CC, 0xFFE2EC, 0 },
    { "willow",      PT_TREE,   28, 0x8CC27A, 0x6FA862, 0 },
    { "birch",       PT_TREE,   26, 0xA9D47F, 0xE8E8E0, 0 },
    { "oak",         PT_TREE,   40, 0x4F8F4A, 0x3E7039, 0 },
    /* rare seeds, found in the ruins */
    { "moonflower",  PT_FLOWER,  3, 0xEEF4FF, 0x9FD8FF, 0 },
    { "crystallily", PT_FLOWER,  4, 0x9FF0F0, 0xFFFFFF, 0 },
    { "lanterntree", PT_TREE,   20, 0x3F6F5A, 0xFFD76A, 1 },
    { "spirittree",  PT_TREE,   36, 0x5FC0B0, 0xBFF7EA, 0 },
};
#define PLANT_COUNT (sizeof(PLANTS) / sizeof(PLANTS[0]))
#define COMMON_COUNT 15             /* the first 15 are the common seeds  */

typedef struct {
    const char *name;
    uint8_t  fly, eats, water, cap;
    uint8_t  speed;                 /* world px per second */
    uint32_t body, mark;
} animal_def_t;

static const animal_def_t ANIMALS[] = {
    { "butterfly", 1, 0, 0, 10, 20, 0xF8D0F0, 0xFFFFFF },
    { "bee",       1, 0, 0,  8, 26, 0xF0C850, 0x201810 },
    { "songbird",  1, 1, 0,  8, 34, 0x7AB8E8, 0xFFE070 },
    { "rabbit",    0, 1, 0,  8, 30, 0xE8DCC8, 0xFFFFFF },
    { "hedgehog",  0, 1, 0,  4, 14, 0x8A7050, 0x4A3A28 },
    { "squirrel",  0, 1, 0,  5, 36, 0xC87840, 0xE8C090 },
    { "duck",      0, 1, 1,  6, 16, 0xF8F0E0, 0xF0A030 },
    { "frog",      0, 0, 1,  6, 12, 0x70B050, 0xD8E870 },
    { "deer",      0, 1, 0,  4, 22, 0xB08050, 0xF0E0C8 },
    { "fox",       0, 1, 0,  2, 30, 0xD87830, 0xFFFFFF },
    { "owl",       1, 0, 0,  2, 18, 0x9A8870, 0xE8D8B0 },
};
#define ANIMAL_COUNT (sizeof(ANIMALS) / sizeof(ANIMALS[0]))

/* Ruins: name, the rare seed they give, footprint. */
typedef struct { const char *name; uint8_t seed, w, h; } ruin_def_t;
static const ruin_def_t RUINS[MAX_RUINS] = {
    { "Moss Fountain",   15, 2, 2 }, { "Singing Stones",  16, 3, 3 },
    { "Root Arch",       17, 2, 1 }, { "Fox Statue",      11, 1, 1 },
    { "Lantern Shrine",  17, 1, 1 }, { "Star Obelisk",    18, 1, 1 },
    { "Wishing Well",    15, 1, 1 }, { "Sundial",         16, 1, 1 },
    { "Giant's Hand",    18, 2, 1 }, { "Glyph Floor",     15, 3, 3 },
    { "Owl of the Gate", 12, 1, 1 },
};

/* ------------------------------------------------------------------ */
/*  Saved state                                                        */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    uint8_t  k, x, y;
    uint8_t  g;                 /* growth, 0..255 */
    uint8_t  fr;                /* fruit on it    */
    uint8_t  watered;
} plant_t;

typedef struct __attribute__((packed)) {
    uint8_t  k, baby;
    int16_t  x, y;              /* world pixels */
    int16_t  tx, ty;            /* wander target */
    int32_t  fed_at;            /* seconds, 0 = never */
} animal_t;

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint32_t seed;
    int64_t  saved;
    uint8_t  tile[GW * GH];
    uint16_t plant_count;
    plant_t  plant[MAX_PLANTS];
    uint8_t  animal_count;
    animal_t animal[MAX_ANIMALS];
    uint8_t  ruin_found[MAX_RUINS];
    uint8_t  seeds[PLANT_COUNT];
    uint8_t  next_seed;
    int16_t  hero_x, hero_y;
} garden_t;

static garden_t *G;

/* Runtime-only: ruin placement is regenerated from the seed, not stored. */
static struct { uint8_t t, x, y, w, h; } s_ruin[MAX_RUINS];

/* Visitors: crystal-generated characters that wander the cleared ground. */
static struct { crystal_char_t ch; float x, y, tx, ty; uint8_t dir, step; } s_npc[MAX_NPCS];
static crystal_char_t s_hero_ch;
static uint8_t  s_hero_dir;
static float    s_hero_step;

static const char *NVS_NS = "garden";
static const char *NVS_KEY = "world";

static inline int tidx(int x, int y) { return y * GW + x; }
static inline uint8_t tile_at(int x, int y)
{
    if ((unsigned)x >= GW || (unsigned)y >= GH) return TILE_MAKE(3, 0);
    return G->tile[tidx(x, y)];
}
static int stage_of(const plant_t *p)
{
    return p->g < 31 ? 0 : p->g < 102 ? 1 : p->g < 204 ? 2 : 3;
}

/* ------------------------------------------------------------------ */
/*  World generation                                                   */
/* ------------------------------------------------------------------ */

static float vnoise(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float smooth_noise(float x, float y, uint32_t seed)
{
    int xi = (int)x, yi = (int)y;
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = vnoise(xi, yi, seed),     b = vnoise(xi + 1, yi, seed);
    float c = vnoise(xi, yi + 1, seed), d = vnoise(xi + 1, yi + 1, seed);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

static void place_ruins(uint32_t seed)
{
    fx_srand(seed ^ 0xBEEF);
    int placed = 0;
    for (int i = 0; i < MAX_RUINS; i++) {
        for (int tries = 0; tries < 60; tries++) {
            float a = (float)(fx_rand() % 628) / 100.0f;
            float d = 8.0f + i * 2.0f + (float)(fx_rand() % 500) / 100.0f;
            int x = MID + (int)(cosf(a) * d), y = MID + (int)(sinf(a) * d);
            if (x < 3 || y < 3 || x + RUINS[i].w >= GW - 3 || y + RUINS[i].h >= GH - 3) continue;

            bool clash = false;
            for (int j = 0; j < placed; j++) {
                int dx = s_ruin[j].x - x, dy = s_ruin[j].y - y;
                if (dx * dx + dy * dy < 49) { clash = true; break; }
            }
            if (clash) continue;

            s_ruin[placed].t = (uint8_t)i;
            s_ruin[placed].x = (uint8_t)x;
            s_ruin[placed].y = (uint8_t)y;
            s_ruin[placed].w = RUINS[i].w;
            s_ruin[placed].h = RUINS[i].h;
            placed++;
            break;
        }
    }
    for (int i = placed; i < MAX_RUINS; i++) s_ruin[i].w = 0;   /* unplaced */
}

static void generate(uint32_t seed)
{
    memset(G, 0, sizeof(*G));
    G->version = 1;
    G->seed    = seed;

    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            float dx = x - MID, dy = y - MID;
            float d = sqrtf(dx * dx + dy * dy);
            float n = smooth_noise(x * 0.12f, y * 0.12f, seed);

            uint8_t wild;
            if      (d < 6.5f) wild = 0;        /* the clearing you start in */
            else if (d < 9.0f) wild = 1;
            else if (n > 0.58f) wild = 3;
            else if (n > 0.42f) wild = 2;
            else wild = (d > 20.0f) ? 2 : 1;
            G->tile[tidx(x, y)] = TILE_MAKE(wild, 0);
        }
    }

    /* Ponds: the first one close enough to find early. */
    fx_srand(seed);
    for (int i = 0; i < 4; i++) {
        float a  = (float)(fx_rand() % 628) / 100.0f;
        float d  = (i == 0) ? 12.0f : 15.0f + (float)(fx_rand() % 2000) / 100.0f;
        float cx = MID + cosf(a) * d, cy = MID + sinf(a) * d;
        float rx = 2.2f + (float)(fx_rand() % 250) / 100.0f;
        float ry = 1.8f + (float)(fx_rand() % 200) / 100.0f;

        for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++) {
            for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
                if (x < 2 || y < 2 || x >= GW - 2 || y >= GH - 2) continue;
                float qx = (x - cx) / rx, qy = (y - cy) / ry;
                float q = qx * qx + qy * qy;
                uint8_t cur = G->tile[tidx(x, y)];
                if (q <= 1.0f)      G->tile[tidx(x, y)] = TILE_MAKE(0, 1);
                else if (q <= 1.7f && TILE_GROUND(cur) != 1)
                                    G->tile[tidx(x, y)] = TILE_MAKE(0, 2);
            }
        }
    }

    place_ruins(seed);
    /* Overgrow the ruins so they have to be uncovered. */
    for (int i = 0; i < MAX_RUINS; i++) {
        if (!s_ruin[i].w) continue;
        for (int y = s_ruin[i].y; y < s_ruin[i].y + s_ruin[i].h; y++)
            for (int x = s_ruin[i].x; x < s_ruin[i].x + s_ruin[i].w; x++) {
                uint8_t v = tile_at(x, y);
                if (TILE_WILD(v) < 2) G->tile[tidx(x, y)] = TILE_MAKE(2, TILE_GROUND(v));
            }
    }

    /* The four plants you inherit, as in the original. */
    const struct { uint8_t k; int dx, dy; uint8_t g; } start[] = {
        { 0, -2,  1, 242 }, { 0, -3,  2, 230 }, { 1,  2,  2, 128 }, { 11, 3, -3, 115 },
    };
    for (unsigned i = 0; i < sizeof(start) / sizeof(start[0]); i++) {
        plant_t *p = &G->plant[G->plant_count++];
        p->k = start[i].k;
        p->x = (uint8_t)(MID + start[i].dx);
        p->y = (uint8_t)(MID + start[i].dy);
        p->g = start[i].g;
    }

    /* A pair of butterflies to start. */
    for (int i = 0; i < 2; i++) {
        animal_t *a = &G->animal[G->animal_count++];
        a->k = 0;
        a->x = (int16_t)((MID - 1 + i * 3) * T);
        a->y = (int16_t)((MID + (i ? -1 : 0)) * T);
        a->tx = a->x; a->ty = a->y;
    }

    const uint8_t seeds[PLANT_COUNT] = { 6, 4, 0, 3, 0, 3, 2, 2, 0, 0, 1, 0, 0, 1 };
    memcpy(G->seeds, seeds, sizeof(seeds));

    G->hero_x = MID * T + 8;
    G->hero_y = (MID + 1) * T;
    G->saved  = (int64_t)time(NULL);
}

/* ------------------------------------------------------------------ */
/*  Census, arrivals, catch-up                                         */
/* ------------------------------------------------------------------ */

typedef struct { int plants, bloom, shrubs, trees, big_trees, ponds, cleared, animals; } census_t;

static void census(census_t *s)
{
    memset(s, 0, sizeof(*s));
    s->plants  = G->plant_count;
    s->animals = G->animal_count;

    for (int i = 0; i < G->plant_count; i++) {
        const plant_t *p = &G->plant[i];
        const plant_def_t *P = &PLANTS[p->k];
        int st = stage_of(p);
        if (P->type == PT_FLOWER && st == 3) s->bloom++;
        if (P->type == PT_SHRUB  && st >= 2) s->shrubs++;
        if (P->type == PT_TREE   && st >= 2) s->trees++;
        if (P->type == PT_TREE   && st == 3) s->big_trees++;
    }
    for (int i = 0; i < GW * GH; i++) {
        if (!TILE_WILD(G->tile[i])) s->cleared++;
        if (TILE_GROUND(G->tile[i]) == 1) s->ponds = 1;
    }
}

/* What draws each animal in — the feed's `need` predicates. */
static bool animal_wants_in(int k, const census_t *s)
{
    switch (k) {
    case 0: return s->bloom >= 2;                                 /* butterfly */
    case 1: return s->bloom >= 5;                                 /* bee       */
    case 2: return s->trees >= 1;                                 /* songbird  */
    case 3: return s->plants >= 6 && s->shrubs >= 1;              /* rabbit    */
    case 4: return s->shrubs >= 2 && s->plants >= 10;             /* hedgehog  */
    case 5: return s->big_trees >= 2;                             /* squirrel  */
    case 6: return s->ponds >= 1;                                 /* duck      */
    case 7: return s->ponds >= 1;                                 /* frog      */
    case 8: return s->trees >= 3 && s->cleared >= 320;            /* deer      */
    case 9: return s->animals >= 12;                              /* fox       */
    default: return s->big_trees >= 3;                            /* owl       */
    }
}

static int count_kind(int k)
{
    int n = 0;
    for (int i = 0; i < G->animal_count; i++) if (G->animal[i].k == k) n++;
    return n;
}

/* A cleared tile near (x,y) with nothing on it. */
static bool free_tile(int x, int y, int r, bool want_water, int *ox, int *oy)
{
    for (int i = 0; i < 40; i++) {
        int tx = x + (int)(fx_rand() % (uint32_t)(2 * r + 1)) - r;
        int ty = y + (int)(fx_rand() % (uint32_t)(2 * r + 1)) - r;
        if (tx < 1 || ty < 1 || tx >= GW - 1 || ty >= GH - 1) continue;

        uint8_t v = tile_at(tx, ty);
        if (TILE_WILD(v)) continue;
        bool water = TILE_GROUND(v) == 1;
        if (want_water != water) continue;

        if (!want_water) {
            bool taken = false;
            for (int j = 0; j < G->plant_count; j++)
                if (G->plant[j].x == tx && G->plant[j].y == ty) { taken = true; break; }
            if (taken) continue;
        }
        *ox = tx; *oy = ty;
        return true;
    }
    return false;
}

static void add_animal(int k, int px, int py, int baby)
{
    if (G->animal_count >= MAX_ANIMALS) return;
    animal_t *a = &G->animal[G->animal_count++];
    a->k = (uint8_t)k;
    a->baby = (uint8_t)baby;
    a->x = (int16_t)px; a->y = (int16_t)py;
    a->tx = a->x; a->ty = a->y;
    a->fed_at = 0;
}

static int arrivals(void)
{
    census_t s;
    census(&s);
    int came = 0;

    for (int k = 0; k < (int)ANIMAL_COUNT; k++) {
        if (!animal_wants_in(k, &s)) continue;
        int have = count_kind(k);
        if (have >= 2) continue;

        for (int i = 0; i < 2 - have; i++) {
            int tx, ty;
            bool ok = ANIMALS[k].water
                    ? free_tile(MID, MID, 24, true, &tx, &ty)
                    : free_tile(MID, MID, 10, false, &tx, &ty);
            if (!ok) continue;
            add_animal(k, tx * T + 8, ty * T + 8, 0);
            came++;
        }
    }
    return came;
}

/* Growth that happened while the device was off, from the wall clock. */
static void catch_up(void)
{
    int64_t now = (int64_t)time(NULL);
    int64_t elapsed = now - G->saved;
    if (elapsed < 60) { G->saved = now; return; }
    if (elapsed > 168 * HR_SEC) elapsed = 168 * HR_SEC;   /* cap at a week */
    float hours = (float)elapsed / HR_SEC;

    int bloomed = 0, sown = 0, born = 0;

    for (int i = 0; i < G->plant_count; i++) {
        plant_t *p = &G->plant[i];
        const plant_def_t *P = &PLANTS[p->k];
        int before = stage_of(p);

        int grow = (int)(255.0f * hours / P->hours);
        p->g = (uint8_t)((p->g + grow > 255) ? 255 : p->g + grow);
        if (before < 3 && stage_of(p) == 3) bloomed++;

        if (P->food && stage_of(p) == 3) {
            int cap = (P->type == PT_TREE) ? 4 : 5;
            int add = (int)(hours / 1.5f);
            p->fr = (uint8_t)((p->fr + add > cap) ? cap : p->fr + add);
        }
    }

    /* Flowers seed themselves into free ground nearby. */
    int original = G->plant_count;
    for (int i = 0; i < original && G->plant_count < MAX_PLANTS; i++) {
        plant_t *p = &G->plant[i];
        if (PLANTS[p->k].type != PT_FLOWER || stage_of(p) < 3) continue;
        if ((float)(fx_rand() % 1000) / 1000.0f > hours / 10.0f) continue;

        int tx, ty;
        if (!free_tile(p->x, p->y, 2, false, &tx, &ty)) continue;
        plant_t *n = &G->plant[G->plant_count++];
        n->k = p->k;
        n->x = (uint8_t)tx; n->y = (uint8_t)ty;
        n->g = (uint8_t)(12 + fx_rand() % 76);
        sown++;
    }

    /* Fed pairs have young. */
    for (int k = 0; k < (int)ANIMAL_COUNT; k++) {
        int adults = 0;
        bool fed = !ANIMALS[k].eats;
        int px = MID * T, py = MID * T;
        for (int i = 0; i < G->animal_count; i++) {
            if (G->animal[i].k != k || G->animal[i].baby) continue;
            adults++;
            px = G->animal[i].x; py = G->animal[i].y;
            if (G->animal[i].fed_at && G->saved - G->animal[i].fed_at < 24 * HR_SEC) fed = true;
        }
        if (adults < 2 || !fed) continue;

        int room = ANIMALS[k].cap - count_kind(k);
        int n = (int)(hours / 8.0f) * (adults / 2) + (hours > 6.0f ? 1 : 0);
        if (n > room) n = room;
        while (n-- > 0) { add_animal(k, px + 6, py + 4, 1); born++; }
    }

    int came = arrivals();
    G->saved = now;

    ESP_LOGI(TAG, "away %.1fh: %d grew, %d sown, %d born, %d arrived",
             (double)hours, bloomed, sown, born, came);
}

/* ------------------------------------------------------------------ */
/*  Persistence                                                        */
/* ------------------------------------------------------------------ */

static void garden_save(void)
{
    if (!G) return;
    G->saved  = (int64_t)time(NULL);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, G, sizeof(*G));
    nvs_commit(h);
    nvs_close(h);
}

static bool garden_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(*G);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, G, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(*G) || G->version != 1) return false;
    place_ruins(G->seed);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Drawing                                                            */
/* ------------------------------------------------------------------ */

static uint16_t C_GRASS[3], C_WILD[3], C_WATER, C_WATER2, C_SAND, C_STONE, C_DOT[4];

static void build_palette(void)
{
    /* The feed's "day" palette. */
    C_GRASS[0] = fx_rgb(0x88, 0xCB, 0x70);
    C_GRASS[1] = fx_rgb(0x7C, 0xC2, 0x65);
    C_GRASS[2] = fx_rgb(0x93, 0xD2, 0x7A);
    C_WILD[0]  = fx_rgb(0x5E, 0xA5, 0x5B);
    C_WILD[1]  = fx_rgb(0x51, 0x97, 0x4F);
    C_WILD[2]  = fx_rgb(0x43, 0x88, 0x43);
    C_WATER    = fx_rgb(0x5A, 0xAE, 0xE6);
    C_WATER2   = fx_rgb(0x9D, 0xD6, 0xF6);
    C_SAND     = fx_rgb(0xEC, 0xD7, 0xA6);
    C_STONE    = fx_rgb(0xBC, 0xB5, 0xAB);
    C_DOT[0]   = fx_rgb(0xFF, 0x6B, 0x8A);
    C_DOT[1]   = fx_rgb(0xFF, 0xD2, 0x3F);
    C_DOT[2]   = fx_rgb(0xFF, 0xFF, 0xFF);
    C_DOT[3]   = fx_rgb(0xB2, 0x8D, 0xFF);
}

static uint16_t pack_rgb(uint32_t v)
{
    return fx_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static void draw_plant(uint16_t *fb, int sx, int sy, const plant_t *p)
{
    const plant_def_t *P = &PLANTS[p->k];
    int st = stage_of(p);
    uint16_t main = pack_rgb(P->main), acc = pack_rgb(P->accent);
    uint16_t stem = fx_rgb(0x4A, 0x82, 0x40);

    int cx = sx + T / 2, base = sy + T - 2;

    if (st == 0) {                                  /* seedling */
        fx_rect(fb, cx - 1, base - 3, 2, 3, stem);
        fx_px(fb, cx - 2, base - 4, main);
        fx_px(fb, cx + 1, base - 4, main);
        return;
    }

    if (P->type == PT_FLOWER) {
        int h = (st == 1) ? 4 : (st == 2) ? 7 : 10;
        fx_rect(fb, cx, base - h, 1, h, stem);
        if (st >= 2) {
            int r = (st == 3) ? 3 : 2;
            fx_disc(fb, cx, base - h - 1, r, main);
            if (st == 3) fx_px(fb, cx, base - h - 1, acc);
        }
    } else if (P->type == PT_SHRUB) {
        int r = (st == 1) ? 3 : (st == 2) ? 5 : 6;
        fx_disc(fb, cx, base - r, r, main);
        if (st == 3) {
            for (int i = 0; i < p->fr && i < 5; i++)
                fx_px(fb, cx - 3 + i * 2, base - r - 1 + (i & 1), acc);
        }
    } else {                                        /* tree */
        int trunk = (st == 1) ? 4 : (st == 2) ? 7 : 10;
        int r     = (st == 1) ? 3 : (st == 2) ? 6 : 8;
        fx_rect(fb, cx - 1, base - trunk, 2, trunk, fx_rgb(0x6B, 0x4A, 0x2B));
        fx_disc(fb, cx, base - trunk - r / 2, r, main);
        if (st == 3 && P->food) {
            for (int i = 0; i < p->fr && i < 4; i++)
                fx_px(fb, cx - 4 + i * 3, base - trunk - r / 2 + 2, acc);
        } else if (st == 3) {
            fx_px(fb, cx - 3, base - trunk - r, acc);
            fx_px(fb, cx + 3, base - trunk - r + 2, acc);
        }
    }
}

static void draw_animal(uint16_t *fb, int sx, int sy, const animal_t *a, uint32_t t)
{
    const animal_def_t *A = &ANIMALS[a->k];
    uint16_t body = pack_rgb(A->body), mark = pack_rgb(A->mark);
    int r = a->baby ? 2 : (A->fly ? 2 : 4);
    int bob = A->fly ? (int)(2.0f * sinf((float)t * 0.012f + a->x)) : 0;

    fx_disc(fb, sx, sy + bob, r, body);
    if (A->fly) {                                   /* wings */
        int w = ((t / 90) & 1) ? 3 : 2;
        fx_disc(fb, sx - r - 1, sy + bob - 1, w, mark);
        fx_disc(fb, sx + r + 1, sy + bob - 1, w, mark);
    } else {
        fx_px(fb, sx + r - 1, sy + bob - 1, mark);  /* eye */
    }
}

static void draw_ruin(uint16_t *fb, int sx, int sy, int w, int h)
{
    fx_rect(fb, sx + 1, sy + 1, w * T - 2, h * T - 2, C_STONE);
    fx_rect(fb, sx + 3, sy + 3, w * T - 6, h * T - 6, fx_rgb(0x9D, 0x95, 0x8B));
    fx_rect(fb, sx + 5, sy + 5, w * T - 10, h * T - 10, fx_rgb(0x6E, 0xA8, 0x5C));
}

static void render(uint32_t t)
{
    uint16_t *fb = bsp_fb();

    int camx = G->hero_x - BSP_FB_W / 2;
    int camy = G->hero_y - BSP_FB_H / 2;
    if (camx < 0) camx = 0;
    if (camy < 0) camy = 0;
    if (camx > WORLD_W - BSP_FB_W) camx = WORLD_W - BSP_FB_W;
    if (camy > WORLD_H - BSP_FB_H) camy = WORLD_H - BSP_FB_H;

    /* ground + overgrowth */
    int t0x = camx / T, t0y = camy / T;
    for (int ty = t0y; ty <= t0y + BSP_FB_H / T + 1 && ty < GH; ty++) {
        for (int tx = t0x; tx <= t0x + BSP_FB_W / T + 1 && tx < GW; tx++) {
            int sx = tx * T - camx, sy = ty * T - camy;
            uint8_t v = G->tile[tidx(tx, ty)];
            int wild = TILE_WILD(v), gnd = TILE_GROUND(v);
            int shade = (tx * 5 + ty * 3) % 3;

            uint16_t c = (gnd == 1) ? C_WATER : (gnd == 2) ? C_SAND : C_GRASS[shade];
            if (wild) c = C_WILD[wild - 1];
            fx_rect(fb, sx, sy, T, T, c);

            if (gnd == 1 && ((tx + ty + t / 400) & 3) == 0)
                fx_rect(fb, sx + 3, sy + 5, 6, 2, C_WATER2);
            else if (!wild && gnd == 0 && ((tx * 7 + ty * 11) % 23) == 0)
                fx_px(fb, sx + 6, sy + 7, C_DOT[(tx + ty) & 3]);
            else if (wild) {
                /* blades leaning out of the tall grass */
                for (int i = 0; i < wild + 1; i++) {
                    int bx = sx + 3 + i * 4, by = sy + 4 + ((tx + i) & 3);
                    fx_rect(fb, bx, by, 1, 6, C_WILD[2]);
                }
            }
        }
    }

    /* uncovered ruins */
    for (int i = 0; i < MAX_RUINS; i++) {
        if (!s_ruin[i].w || !G->ruin_found[i]) continue;
        int sx = s_ruin[i].x * T - camx, sy = s_ruin[i].y * T - camy;
        if (sx < -64 || sy < -64 || sx > BSP_FB_W || sy > BSP_FB_H) continue;
        draw_ruin(fb, sx, sy, s_ruin[i].w, s_ruin[i].h);
    }

    /* plants */
    for (int i = 0; i < G->plant_count; i++) {
        int sx = G->plant[i].x * T - camx, sy = G->plant[i].y * T - camy;
        if (sx < -T || sy < -T * 2 || sx > BSP_FB_W || sy > BSP_FB_H) continue;
        draw_plant(fb, sx, sy, &G->plant[i]);
    }

    /* animals */
    for (int i = 0; i < G->animal_count; i++) {
        int sx = G->animal[i].x - camx, sy = G->animal[i].y - camy;
        if (sx < -16 || sy < -16 || sx > BSP_FB_W + 16 || sy > BSP_FB_H + 16) continue;
        draw_animal(fb, sx, sy, &G->animal[i], t);
    }

    /* visitors, then the gardener on top */
    for (int i = 0; i < MAX_NPCS; i++) {
        int sx = (int)s_npc[i].x - camx, sy = (int)s_npc[i].y - camy;
        if (sx < -32 || sy < -40 || sx > BSP_FB_W + 32 || sy > BSP_FB_H + 32) continue;
        crystal_draw(fb, sx, sy, &s_npc[i].ch, s_npc[i].dir, s_npc[i].step);
    }
    crystal_draw(fb, G->hero_x - camx, G->hero_y - camy, &s_hero_ch, s_hero_dir,
                 ((int)s_hero_step & 1));
}

/* ------------------------------------------------------------------ */
/*  Simulation                                                         */
/* ------------------------------------------------------------------ */

static bool walkable_px(int px, int py)
{
    int tx = px / T, ty = py / T;
    if ((unsigned)tx >= GW || (unsigned)ty >= GH) return false;
    uint8_t v = G->tile[tidx(tx, ty)];
    return TILE_WILD(v) == 0 && TILE_GROUND(v) != 1;
}

static void move_animals(uint32_t dt)
{
    for (int i = 0; i < G->animal_count; i++) {
        animal_t *a = &G->animal[i];
        const animal_def_t *A = &ANIMALS[a->k];

        int dx = a->tx - a->x, dy = a->ty - a->y;
        if (dx * dx + dy * dy < 36) {
            /* pick somewhere new to drift to */
            int tx, ty;
            if (free_tile(a->x / T, a->y / T, 5, A->water, &tx, &ty)) {
                a->tx = (int16_t)(tx * T + 8);
                a->ty = (int16_t)(ty * T + 8);
            }
            continue;
        }
        float len = sqrtf((float)(dx * dx + dy * dy));
        float step = A->speed * (float)dt / 1000.0f;
        a->x = (int16_t)(a->x + dx / len * step);
        a->y = (int16_t)(a->y + dy / len * step);
    }
}

static void move_npcs(uint32_t dt)
{
    for (int i = 0; i < MAX_NPCS; i++) {
        float dx = s_npc[i].tx - s_npc[i].x, dy = s_npc[i].ty - s_npc[i].y;
        float len = sqrtf(dx * dx + dy * dy);

        if (len < 6.0f) {
            int tx, ty;
            if (free_tile(MID, MID, 14, false, &tx, &ty)) {
                s_npc[i].tx = (float)(tx * T + 8);
                s_npc[i].ty = (float)(ty * T + T);
            }
            continue;
        }
        float step = 22.0f * (float)dt / 1000.0f;
        s_npc[i].x += dx / len * step;
        s_npc[i].y += dy / len * step;
        s_npc[i].dir = (uint8_t)crystal_dir_for(dx, dy);
        s_npc[i].step = (uint8_t)(((int)(s_npc[i].x + s_npc[i].y) / 6) & 1);
    }
}

/* Growth while the device is on, at the same rate as the catch-up. */
static void grow_live(uint32_t dt)
{
    static uint32_t accum;
    accum += dt;
    if (accum < 1000) return;
    float hours = (float)accum / (HR_SEC * 1000.0f);
    accum = 0;

    for (int i = 0; i < G->plant_count; i++) {
        plant_t *p = &G->plant[i];
        /* Watering doubles the rate for a while — the one reason to carry a can. */
        float rate = p->watered ? 2.0f : 1.0f;
        int grow = (int)(255.0f * hours * rate / PLANTS[p->k].hours);
        if (grow < 1 && (fx_rand() & 0xFF) < 8) grow = 1;   /* never fully stall */
        int g = p->g + grow;
        p->g = (uint8_t)(g > 255 ? 255 : g);
        if (p->watered && p->watered < 255) p->watered--;
    }
}

/* ------------------------------------------------------------------ */
/*  The one verb                                                       */
/* ------------------------------------------------------------------ */

static void reveal_ruin_at(int tx, int ty)
{
    for (int i = 0; i < MAX_RUINS; i++) {
        if (!s_ruin[i].w || G->ruin_found[i]) continue;
        if (tx < s_ruin[i].x || tx >= s_ruin[i].x + s_ruin[i].w) continue;
        if (ty < s_ruin[i].y || ty >= s_ruin[i].y + s_ruin[i].h) continue;

        /* Uncovered only once every tile of it is clear. */
        for (int y = s_ruin[i].y; y < s_ruin[i].y + s_ruin[i].h; y++)
            for (int x = s_ruin[i].x; x < s_ruin[i].x + s_ruin[i].w; x++)
                if (TILE_WILD(tile_at(x, y))) return;

        G->ruin_found[i] = 1;
        uint8_t seed = RUINS[s_ruin[i].t].seed;
        if (G->seeds[seed] < 9) G->seeds[seed] += 2;
        ESP_LOGI(TAG, "uncovered %s — rare seed: %s",
                 RUINS[s_ruin[i].t].name, PLANTS[seed].name);
        return;
    }
}

static void garden_action(void)
{
    if (!G) return;
    int hx = G->hero_x / T, hy = G->hero_y / T;

    /* 1. standing in tall grass: clear it */
    uint8_t v = tile_at(hx, hy);
    if (TILE_WILD(v)) {
        G->tile[tidx(hx, hy)] = TILE_MAKE(0, TILE_GROUND(v));
        reveal_ruin_at(hx, hy);
        ESP_LOGI(TAG, "cleared grass at %d,%d", hx, hy);
        return;
    }

    /* 2. an animal within a tile: feed it */
    for (int i = 0; i < G->animal_count; i++) {
        int dx = G->animal[i].x - G->hero_x, dy = G->animal[i].y - G->hero_y;
        if (dx * dx + dy * dy > (T * 2) * (T * 2)) continue;
        if (!ANIMALS[G->animal[i].k].eats) continue;
        G->animal[i].fed_at = (int32_t)time(NULL);
        ESP_LOGI(TAG, "fed a %s", ANIMALS[G->animal[i].k].name);
        return;
    }

    /* 3. a plant within a tile that is dry: water it */
    for (int i = 0; i < G->plant_count; i++) {
        int dx = G->plant[i].x - hx, dy = G->plant[i].y - hy;
        if (dx * dx + dy * dy > 2 || G->plant[i].watered) continue;
        G->plant[i].watered = 200;
        ESP_LOGI(TAG, "watered the %s", PLANTS[G->plant[i].k].name);
        return;
    }

    /* 4. otherwise plant the next seed in your pocket */
    if (TILE_GROUND(v) != 0 || G->plant_count >= MAX_PLANTS) return;
    for (int i = 0; i < G->plant_count; i++)
        if (G->plant[i].x == hx && G->plant[i].y == hy) return;

    for (int n = 0; n < (int)PLANT_COUNT; n++) {
        int k = (G->next_seed + n) % PLANT_COUNT;
        if (!G->seeds[k]) continue;
        G->seeds[k]--;
        G->next_seed = (uint8_t)((k + 1) % PLANT_COUNT);

        plant_t *p = &G->plant[G->plant_count++];
        p->k = (uint8_t)k;
        p->x = (uint8_t)hx; p->y = (uint8_t)hy;
        p->g = 4;
        ESP_LOGI(TAG, "planted a %s", PLANTS[k].name);
        return;
    }
    ESP_LOGI(TAG, "no seeds left");
}

/* ------------------------------------------------------------------ */
/*  Mode interface                                                     */
/* ------------------------------------------------------------------ */

static void garden_enter(void)
{
    G = heap_caps_malloc(sizeof(garden_t), MALLOC_CAP_SPIRAM);
    if (!G) {
        ESP_LOGE(TAG, "no room for the garden (%u bytes)", (unsigned)sizeof(garden_t));
        return;
    }
    build_palette();

    if (!garden_load()) {
        ESP_LOGI(TAG, "new garden");
        generate(fx_rand());
    }
    catch_up();

    crystal_make(&s_hero_ch, G->seed ^ 0x5EED);
    for (int i = 0; i < MAX_NPCS; i++) {
        crystal_make(&s_npc[i].ch, G->seed + i * 7919u + 13u);
        s_npc[i].x = s_npc[i].tx = (float)((MID + i - 2) * T + 8);
        s_npc[i].y = s_npc[i].ty = (float)((MID + 3 + i) * T);
        s_npc[i].dir = CRYS_S;
    }
    ESP_LOGI(TAG, "garden: %d plants, %d animals", G->plant_count, G->animal_count);
}

static void garden_leave(void)
{
    garden_save();
    free(G);
    G = NULL;
}

static void garden_tick(uint32_t dt)
{
    if (!G) return;
    static uint32_t clock_ms, save_ms;
    clock_ms += dt;

    /* Walk toward the finger. */
    uint16_t tx, ty;
    if (bsp_touch_get_point(&tx, &ty)) {
        float dx = (float)tx - BSP_FB_W / 2.0f;
        float dy = (float)ty - BSP_FB_H / 2.0f;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 10.0f) {
            float step = 0.075f * (float)dt;
            if (step > 5.0f) step = 5.0f;
            int nx = G->hero_x + (int)(dx / len * step);
            int ny = G->hero_y + (int)(dy / len * step);
            if (walkable_px(nx, G->hero_y)) G->hero_x = (int16_t)nx;
            if (walkable_px(G->hero_x, ny)) G->hero_y = (int16_t)ny;
            s_hero_dir  = (uint8_t)crystal_dir_for(dx, dy);
            s_hero_step += (float)dt * 0.012f;
        }
    }

    grow_live(dt);
    move_animals(dt);
    move_npcs(dt);
    render(clock_ms);

    /* Keep the garden safe without thrashing flash. */
    save_ms += dt;
    if (save_ms > 30000) { save_ms = 0; garden_save(); }
}

const chaos_mode_t mode_garden = {
    .name = "garden",
    .enter = garden_enter, .leave = garden_leave,
    .tick = garden_tick,   .action = garden_action,
};
