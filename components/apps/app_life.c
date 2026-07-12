/*
 * app_life.c — "PRIMORDIAL": a cellular-automata sandbox.
 *
 * A 58x58 toroidal grid clipped to the round display, rendered pixel-exact
 * into an LVGL canvas (each cell an 8x8 block). Draw on it with a finger,
 * cycle between life-like rules, and shake the device to reseed with noise.
 */
#include "apps.h"
#include "chaos_os.h"
#include "theme.h"

#include "esp_heap_caps.h"
#include "esp_random.h"
#include <stdlib.h>
#include <string.h>

#define GW    58
#define GH    58
#define CELL  8
#define DIM   (GW * CELL)   /* 464, centred on the 466 panel */

/* Life-like rules as neighbour-count bitmasks: bit n set => n neighbours acts. */
typedef struct { const char *name; uint16_t birth, survive; } rule_t;
static const rule_t RULES[] = {
    { "Conway  B3/S23",   1u << 3,            (1u << 2) | (1u << 3) },
    { "HighLife B36/S23", (1u << 3) | (1u << 6), (1u << 2) | (1u << 3) },
    { "Seeds   B2/S",     1u << 2,            0 },
    { "Maze    B3/S12345", 1u << 3,           0x3E /* bits 1..5 */ },
};
#define NUM_RULES (sizeof(RULES) / sizeof(RULES[0]))

typedef struct {
    uint8_t     cur[GH][GW];
    uint8_t     nxt[GH][GW];
    uint8_t     mask[GH][GW];     /* 1 inside the circle */
    lv_color_t *buf;              /* DIM*DIM framebuffer in PSRAM */
    lv_obj_t   *canvas;
    lv_obj_t   *rule_lbl;
    lv_obj_t   *play_lbl;
    bool        running;
    int         rule;
    uint32_t    accent;
    uint32_t    accum_ms;
    uint32_t    interval_ms;
} life_t;

/* ---- grid helpers ---------------------------------------------------- */

static void build_mask(life_t *L)
{
    const float cx = (GW - 1) / 2.0f, cy = (GH - 1) / 2.0f;
    const float r2 = (GW / 2.0f - 0.5f) * (GH / 2.0f - 0.5f);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            float dx = x - cx, dy = y - cy;
            L->mask[y][x] = (dx * dx + dy * dy) <= r2;
        }
}

static void randomize(life_t *L)
{
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            L->cur[y][x] = L->mask[y][x] && (esp_random() & 3) == 0;
}

static void clear_grid(life_t *L)
{
    memset(L->cur, 0, sizeof(L->cur));
}

static void fill_cell(life_t *L, int gx, int gy, lv_color_t c)
{
    int x0 = gx * CELL, y0 = gy * CELL;
    for (int y = 0; y < CELL - 1; y++) {          /* leave a 1px grid gap */
        lv_color_t *row = &L->buf[(y0 + y) * DIM + x0];
        for (int x = 0; x < CELL - 1; x++) row[x] = c;
    }
}

static void render(life_t *L)
{
    lv_color_t bg   = lv_color_hex(0x05070A);
    lv_color_t off  = lv_color_hex(0x0D1016);
    lv_color_t on   = lv_color_hex(L->accent);
    /* clear whole buffer to background */
    for (int i = 0; i < DIM * DIM; i++) L->buf[i] = bg;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            if (!L->mask[y][x]) continue;
            fill_cell(L, x, y, L->cur[y][x] ? on : off);
        }
    lv_obj_invalidate(L->canvas);
}

static void step(life_t *L)
{
    const rule_t *r = &RULES[L->rule];
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            if (!L->mask[y][x]) { L->nxt[y][x] = 0; continue; }
            int n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int nx = (x + dx + GW) % GW, ny = (y + dy + GH) % GH;
                    if (L->mask[ny][nx]) n += L->cur[ny][nx];
                }
            uint16_t bit = 1u << n;
            L->nxt[y][x] = L->cur[y][x] ? (r->survive & bit) != 0
                                        : (r->birth   & bit) != 0;
        }
    memcpy(L->cur, L->nxt, sizeof(L->cur));
}

/* ---- UI callbacks ---------------------------------------------------- */

static void on_paint(lv_event_t *e)
{
    life_t *L = (life_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(L->canvas, &a);
    int gx = (p.x - a.x1) / CELL, gy = (p.y - a.y1) / CELL;
    if (gx < 0 || gx >= GW || gy < 0 || gy >= GH || !L->mask[gy][gx]) return;
    L->cur[gy][gx] = 1;
    fill_cell(L, gx, gy, lv_color_hex(L->accent));
    lv_obj_invalidate(L->canvas);
}

static void update_labels(life_t *L)
{
    lv_label_set_text(L->rule_lbl, RULES[L->rule].name);
    lv_label_set_text(L->play_lbl, L->running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void btn_play(lv_event_t *e) { life_t *L = lv_event_get_user_data(e); L->running = !L->running; update_labels(L); }
static void btn_rand(lv_event_t *e) { life_t *L = lv_event_get_user_data(e); randomize(L); render(L); }
static void btn_clear(lv_event_t *e){ life_t *L = lv_event_get_user_data(e); clear_grid(L); render(L); }
static void btn_rule(lv_event_t *e) { life_t *L = lv_event_get_user_data(e); L->rule = (L->rule + 1) % NUM_RULES; update_labels(L); }

static lv_obj_t *mini_btn(lv_obj_t *bar, const char *sym, lv_event_cb_t cb, void *u, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_btn_create(bar);
    lv_obj_set_size(b, 46, 46);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1D22), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, u);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

/* ---- lifecycle ------------------------------------------------------- */

static void life_open(chaos_app_t *app)
{
    life_t *L = calloc(1, sizeof(life_t));
    L->accent      = app->desc->accent;
    L->running     = true;
    L->interval_ms = 110;
    app->user      = L;

    build_mask(L);
    randomize(L);

    L->buf = heap_caps_malloc(DIM * DIM * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!L->buf) L->buf = heap_caps_malloc(DIM * DIM * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
    if (!L->buf) {                       /* no room for the framebuffer */
        lv_obj_t *err = lv_label_create(app->root);
        lv_label_set_text(err, "out of memory");
        lv_obj_center(err);
        return;
    }
    L->canvas = lv_canvas_create(app->root);
    lv_canvas_set_buffer(L->canvas, L->buf, DIM, DIM, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(L->canvas);
    lv_obj_add_flag(L->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(L->canvas, on_paint, LV_EVENT_PRESSING, L);

    /* floating control bar near the top of the round face */
    lv_obj_t *bar = lv_obj_create(app->root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 26);
    mini_btn(bar, LV_SYMBOL_PLAY,    btn_play,  L, &L->play_lbl);
    mini_btn(bar, LV_SYMBOL_REFRESH, btn_rand,  L, NULL);
    mini_btn(bar, LV_SYMBOL_TRASH,   btn_clear, L, NULL);
    mini_btn(bar, LV_SYMBOL_LOOP,    btn_rule,  L, NULL);

    L->rule_lbl = lv_label_create(app->root);
    lv_obj_set_style_text_color(L->rule_lbl, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_text_font(L->rule_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(L->rule_lbl, LV_ALIGN_TOP_MID, 0, 80);

    update_labels(L);
    render(L);
}

static void life_tick(chaos_app_t *app, uint32_t dt)
{
    life_t *L = app->user;
    if (!L->canvas || !L->running) return;
    L->accum_ms += dt;
    if (L->accum_ms >= L->interval_ms) {
        L->accum_ms = 0;
        step(L);
        render(L);
    }
}

static void life_event(chaos_app_t *app, const chaos_event_t *ev)
{
    if (ev->type == CHAOS_EV_SHAKE) {   /* shake = big bang */
        life_t *L = app->user;
        if (!L->canvas) return;
        randomize(L);
        render(L);
    }
}

static void life_close(chaos_app_t *app)
{
    life_t *L = app->user;
    if (L) {
        if (L->buf) heap_caps_free(L->buf);
        free(L);
    }
}

const chaos_app_desc_t app_life_desc = {
    .id = "life", .name = "Primordial", .glyph = "life", .accent = 0x37E0A6,
    .on_open = life_open, .on_close = life_close,
    .on_tick = life_tick, .on_event = life_event,
};
