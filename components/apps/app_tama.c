/*
 * app_tama.c — "GLORP": a tamagotchi.
 *
 * A needy blob with hunger/fun/rest/hygiene/health stats that decay in real
 * time — even while the device is off, reconstructed from a wall-clock stamp on
 * open. Feed / Play / Clean / Sleep to keep it alive; neglect it and it dies.
 * State lives in NVS, so Glorp remembers.
 */
#include "apps.h"
#include "chaos_os.h"
#include "theme.h"

#include "nvs.h"
#include "esp_random.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define STAT_MAX     100
#define STEP_SEC     4        /* one decay step every 4 s of life */
#define OFFLINE_CAP  400      /* don't decay more than this many steps at once */

typedef struct __attribute__((packed)) {
    int16_t hunger, fun, rest, wash, health;  /* 0..100 (higher = better) */
    int32_t age_steps;
    int16_t poop;
    uint8_t alive, asleep;
    int64_t last_ts;                          /* wall clock at last save */
} tama_save_t;

typedef struct {
    tama_save_t s;
    lv_obj_t *body, *eye_l, *eye_r, *mouth, *poop_obj, *status, *reset, *acts;
    lv_obj_t *bar[4];                         /* hunger, fun, rest, wash */
    uint32_t  accum_ms, anim_ms;
    uint32_t  accent;
    bool      blink;
} tama_t;

static const char *NVS_NS = "glorp";
static const char *NVS_KEY = "pet";

/* ---- persistence ----------------------------------------------------- */

static void defaults(tama_save_t *s)
{
    s->hunger = s->fun = s->rest = s->wash = s->health = 80;
    s->age_steps = 0;
    s->poop = 0;
    s->alive = 1;
    s->asleep = 0;
    s->last_ts = time(NULL);
}

static void load(tama_save_t *s)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(*s);
        if (nvs_get_blob(h, NVS_KEY, s, &len) == ESP_OK && len == sizeof(*s)) {
            nvs_close(h);
            return;
        }
        nvs_close(h);
    }
    defaults(s);
}

static void save(tama_save_t *s)
{
    s->last_ts = time(NULL);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, s, sizeof(*s));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- simulation ------------------------------------------------------ */

static int clampi(int v) { return v < 0 ? 0 : (v > STAT_MAX ? STAT_MAX : v); }

static void decay_step(tama_save_t *s)
{
    if (!s->alive) return;
    s->age_steps++;

    if (s->asleep) {
        s->rest = clampi(s->rest + 4);
        s->hunger = clampi(s->hunger - 1);
        if (s->rest >= STAT_MAX) s->asleep = 0;   /* wakes when rested */
    } else {
        s->hunger = clampi(s->hunger - 2);
        s->fun    = clampi(s->fun - 1);
        s->rest   = clampi(s->rest - 1);
    }
    s->wash = clampi(s->wash - 1 - s->poop);      /* mess makes it dirtier */

    if ((esp_random() % 12) == 0) s->poop = (s->poop < 4) ? s->poop + 1 : 4;

    bool suffering = (s->hunger == 0) || (s->wash <= 5) || (s->fun == 0) || (s->rest == 0);
    if (suffering)                 s->health = clampi(s->health - 3);
    else if (s->hunger > 60 && s->fun > 50 && s->wash > 50)
                                   s->health = clampi(s->health + 1);
    if (s->health <= 0) s->alive = 0;
}

/* ---- face / UI ------------------------------------------------------- */

static uint32_t body_color(tama_t *t)
{
    if (!t->s.alive)          return 0x4A4A52;   /* grey                 */
    if (t->s.health < 30)     return 0x8E7CC3;   /* sickly pale purple   */
    return t->accent;
}

static void set_eye(lv_obj_t *eye, bool open, bool dead)
{
    if (dead) {
        lv_obj_set_size(eye, 22, 22);
        lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
        return;
    }
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_size(eye, 22, open ? 22 : 4);
}

/* Draw the mouth as an arc curving up (smile), down (frown) or flat. */
static void set_mouth(lv_obj_t *m, int mood)  /* +1 smile, 0 flat, -1 frown */
{
    lv_obj_set_style_arc_opa(m, mood == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    if (mood >= 0) lv_arc_set_bg_angles(m, 20, 160);    /* lower curve = smile */
    else           lv_arc_set_bg_angles(m, 200, 340);   /* upper curve = frown */
}

static const char *status_text(tama_t *t)
{
    tama_save_t *s = &t->s;
    if (!s->alive)        return "R.I.P. Glorp";
    if (s->asleep)        return "zzz...";
    if (s->health < 30)   return "feeling awful";
    if (s->hunger < 25)   return "SO hungry";
    if (s->wash < 25)     return "eww, dirty";
    if (s->rest < 25)     return "sleepy";
    if (s->fun < 25)      return "bored";
    if (s->fun > 70 && s->hunger > 60) return "having a blast!";
    return "content";
}

static void refresh(tama_t *t)
{
    tama_save_t *s = &t->s;
    lv_obj_set_style_bg_color(t->body, lv_color_hex(body_color(t)), 0);

    bool dead = !s->alive;
    bool eyes_open = s->alive && !s->asleep && !t->blink;
    set_eye(t->eye_l, eyes_open, dead);
    set_eye(t->eye_r, eyes_open, dead);
    lv_obj_set_style_arc_color(t->mouth, lv_color_hex(dead ? 0x202024 : 0x101014), LV_PART_MAIN);

    int mood = 0;
    if (!s->alive || s->health < 30 || s->hunger < 25) mood = -1;
    else if (s->fun > 60 && s->hunger > 50)            mood = 1;
    set_mouth(t->mouth, mood);

    lv_bar_set_value(t->bar[0], s->hunger, LV_ANIM_OFF);
    lv_bar_set_value(t->bar[1], s->fun,    LV_ANIM_OFF);
    lv_bar_set_value(t->bar[2], s->rest,   LV_ANIM_OFF);
    lv_bar_set_value(t->bar[3], s->wash,   LV_ANIM_OFF);

    if (s->poop > 0 && s->alive) lv_obj_clear_flag(t->poop_obj, LV_OBJ_FLAG_HIDDEN);
    else                         lv_obj_add_flag(t->poop_obj, LV_OBJ_FLAG_HIDDEN);

    if (t->reset && t->acts) {
        if (s->alive) {
            lv_obj_add_flag(t->reset, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(t->acts, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(t->reset, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(t->acts, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_label_set_text(t->status, status_text(t));
}

/* ---- buttons --------------------------------------------------------- */

static void act_feed(lv_event_t *e)  { tama_t *t = lv_event_get_user_data(e); if (t->s.alive){ t->s.hunger = clampi(t->s.hunger + 30); if ((esp_random()&3)==0 && t->s.poop<4) t->s.poop++; refresh(t);} }
static void act_play(lv_event_t *e)  { tama_t *t = lv_event_get_user_data(e); if (t->s.alive && !t->s.asleep){ t->s.fun = clampi(t->s.fun + 25); t->s.rest = clampi(t->s.rest - 12); refresh(t);} }
static void act_clean(lv_event_t *e) { tama_t *t = lv_event_get_user_data(e); if (t->s.alive){ t->s.poop = 0; t->s.wash = STAT_MAX; refresh(t);} }
static void act_sleep(lv_event_t *e) { tama_t *t = lv_event_get_user_data(e); if (t->s.alive){ t->s.asleep = !t->s.asleep; refresh(t);} }

static void act_reset(lv_event_t *e) { tama_t *t = lv_event_get_user_data(e); defaults(&t->s); refresh(t); }

/* A compact round action button with a single-letter label. */
static void action_btn(lv_obj_t *parent, const char *letter, uint32_t col,
                       lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, letter);
    lv_obj_set_style_text_color(l, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_center(l);
}

/* ---- build ----------------------------------------------------------- */

static lv_obj_t *stat_bar(lv_obj_t *parent, const char *tag, uint32_t col)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, 58, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, tag);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *bar = lv_bar_create(wrap);
    lv_obj_set_size(bar, 50, 8);
    lv_bar_set_range(bar, 0, STAT_MAX);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x24252B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(col), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    return bar;
}

static void tama_open(chaos_app_t *app)
{
    tama_t *t = calloc(1, sizeof(tama_t));
    t->accent = app->desc->accent;
    app->user = t;

    load(&t->s);
    /* Catch up on time spent powered off. */
    int64_t now = time(NULL);
    int64_t elapsed = now - t->s.last_ts;
    if (elapsed > 0) {
        int steps = (int)(elapsed / STEP_SEC);
        if (steps > OFFLINE_CAP) steps = OFFLINE_CAP;
        for (int i = 0; i < steps; i++) decay_step(&t->s);
    }

    /* stat bars along the top */
    lv_obj_t *bars = lv_obj_create(app->root);
    lv_obj_remove_style_all(bars);
    lv_obj_set_size(bars, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bars, 6, 0);
    lv_obj_align(bars, LV_ALIGN_TOP_MID, 0, 40);
    t->bar[0] = stat_bar(bars, "food", 0xFFB454);
    t->bar[1] = stat_bar(bars, "fun",  0x54B4FF);
    t->bar[2] = stat_bar(bars, "rest", 0xB58CFF);
    t->bar[3] = stat_bar(bars, "wash", 0x54FFD1);

    /* creature body */
    t->body = lv_obj_create(app->root);
    lv_obj_set_size(t->body, 180, 180);
    lv_obj_set_style_radius(t->body, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(t->body, 0, 0);
    lv_obj_set_style_shadow_width(t->body, 0, 0);
    lv_obj_clear_flag(t->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(t->body, LV_ALIGN_CENTER, 0, -6);

    t->eye_l = lv_obj_create(t->body);
    t->eye_r = lv_obj_create(t->body);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *eye = i ? t->eye_r : t->eye_l;
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(0x101014), 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_align(eye, LV_ALIGN_CENTER, i ? 30 : -30, -18);
    }

    t->mouth = lv_arc_create(t->body);
    lv_obj_set_size(t->mouth, 70, 70);
    lv_obj_clear_flag(t->mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(t->mouth, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(t->mouth, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(t->mouth, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_align(t->mouth, LV_ALIGN_CENTER, 0, 22);

    t->poop_obj = lv_obj_create(app->root);
    lv_obj_set_size(t->poop_obj, 26, 20);
    lv_obj_set_style_radius(t->poop_obj, 10, 0);
    lv_obj_set_style_bg_color(t->poop_obj, lv_color_hex(0x6B4A2B), 0);
    lv_obj_set_style_border_width(t->poop_obj, 0, 0);
    lv_obj_align(t->poop_obj, LV_ALIGN_CENTER, 92, 70);

    t->status = lv_label_create(app->root);
    lv_obj_set_style_text_color(t->status, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_set_style_text_font(t->status, &lv_font_montserrat_20, 0);
    lv_obj_align(t->status, LV_ALIGN_CENTER, 0, 108);

    /* action buttons above the home button */
    t->acts = lv_obj_create(app->root);
    lv_obj_remove_style_all(t->acts);
    lv_obj_set_size(t->acts, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(t->acts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(t->acts, 8, 0);
    lv_obj_align(t->acts, LV_ALIGN_BOTTOM_MID, 0, -78);
    action_btn(t->acts, "F", 0xFFB454, act_feed,  t);   /* Feed  */
    action_btn(t->acts, "P", 0x54B4FF, act_play,  t);   /* Play  */
    action_btn(t->acts, "C", 0x54FFD1, act_clean, t);   /* Clean */
    action_btn(t->acts, "S", 0xB58CFF, act_sleep, t);   /* Sleep */

    /* reset appears only once Glorp has passed on (reuses the action row spot) */
    t->reset = theme_button(app->root, "New Glorp", CHAOS_COL_DANGER, act_reset, t);
    lv_obj_align(t->reset, LV_ALIGN_BOTTOM_MID, 0, -82);
    lv_obj_add_flag(t->reset, LV_OBJ_FLAG_HIDDEN);

    refresh(t);
}

static void tama_tick(chaos_app_t *app, uint32_t dt)
{
    tama_t *t = app->user;
    t->accum_ms += dt;
    t->anim_ms  += dt;

    /* idle bob + occasional blink */
    if (t->anim_ms >= 220) {
        t->anim_ms = 0;
        if (t->s.alive && !t->s.asleep) {
            t->blink = (esp_random() % 8) == 0;
            int dy = (esp_random() % 2) ? -6 : -2;
            lv_obj_align(t->body, LV_ALIGN_CENTER, 0, dy);
            refresh(t);
        }
    }

    if (t->accum_ms >= STEP_SEC * 1000) {
        t->accum_ms = 0;
        decay_step(&t->s);
        save(&t->s);
        refresh(t);
    }
}

static void tama_close(chaos_app_t *app)
{
    tama_t *t = app->user;
    if (t) { save(&t->s); free(t); }
}

const chaos_app_desc_t app_tama_desc = {
    .id = "tama", .name = "Glorp", .glyph = "GL", .accent = 0xFF8FB1,
    .on_open = tama_open, .on_close = tama_close, .on_tick = tama_tick,
};
