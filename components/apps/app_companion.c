/*
 * app_companion.c — "ODDBALL": a weird companion.
 *
 * A single enormous eye that lives in the device. It follows the way you hold
 * it (IMU tilt), blinks, drifts, and mutters cryptic things. Shake it and it
 * startles; tap it and it says something. It keeps count — in NVS — of how many
 * times you've met and how many times you've shaken it, and it will bring that
 * up. Unsettling by design.
 */
#include "apps.h"
#include "chaos_os.h"
#include "theme.h"

#include "nvs.h"
#include "esp_random.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct {
    lv_obj_t *sclera, *iris, *pupil, *lid, *speech, *meta;
    float     tgt_x, tgt_y, cur_x, cur_y;   /* pupil offset (px) */
    float     agit;                          /* 0 calm .. 1 agitated */
    float     dilate;                        /* 1.0 .. ~1.6 */
    uint32_t  phrase_ms, blink_ms, next_blink_ms, blink_left_ms;
    float     idle_phase;                    /* slow wander, anti burn-in */
    uint32_t  accent;
    char      buf[72];
    int32_t   encounters;
    int64_t   shakes;
} odd_t;

static const char *NVS_NS = "oddball";

static const char *CALM[] = {
    "i see you.",
    "the walls remember everything.",
    "you are mostly water. i find that funny.",
    "blink twice if you are real.",
    "i had a dream. it was your ceiling.",
    "somewhere, a clock is wrong on purpose.",
};
static const char *AGIT[] = {
    "TOO BRIGHT. too loud.",
    "everything is vibrating. even you.",
    "stop. STOP. ...okay.",
    "i felt that in teeth i do not have.",
};
static const char *STARTLED[] = {
    "!!!",
    "AH— you scared me.",
    "why would you DO that.",
    "my eye nearly rolled out.",
};

static const char *pick(const char *const *arr, int n)
{
    return arr[esp_random() % n];
}

/* ---- persistence ----------------------------------------------------- */

static void odd_load(odd_t *o, int64_t *last_seen)
{
    o->encounters = 0; o->shakes = 0; *last_seen = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "enc", &o->encounters);
        nvs_get_i64(h, "shk", &o->shakes);
        nvs_get_i64(h, "seen", last_seen);
        nvs_close(h);
    }
}

static void odd_save(odd_t *o)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "enc", o->encounters);
        nvs_set_i64(h, "shk", o->shakes);
        nvs_set_i64(h, "seen", (int64_t)time(NULL));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- visuals --------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t mix(uint32_t a, uint32_t b, float t){
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return (r << 16) | (g << 8) | bl;
}

static void say(odd_t *o, const char *text)
{
    lv_label_set_text(o->speech, text);
}

static void update_meta(odd_t *o)
{
    snprintf(o->buf, sizeof(o->buf), "encounter #%ld   shakes: %lld",
             (long)o->encounters, (long long)o->shakes);
    lv_label_set_text(o->meta, o->buf);
}

/* ---- lifecycle ------------------------------------------------------- */

static void eye_tap(lv_event_t *e)
{
    odd_t *o = lv_event_get_user_data(e);
    o->dilate = 1.5f;
    o->blink_left_ms = 130;
    /* one in four taps, it gets personal */
    if ((esp_random() & 3) == 0) {
        snprintf(o->buf, sizeof(o->buf), "you have shaken me %lld times. i count.",
                 (long long)o->shakes);
        say(o, o->buf);
    } else {
        say(o, o->agit > 0.5f ? pick(AGIT, 4) : pick(CALM, 6));
    }
}

static void odd_open(chaos_app_t *app)
{
    odd_t *o = calloc(1, sizeof(odd_t));
    o->accent  = app->desc->accent;
    o->dilate  = 1.0f;
    o->next_blink_ms = 2500;
    app->user  = o;

    int64_t last_seen = 0;
    odd_load(o, &last_seen);
    o->encounters++;

    /* sclera — deliberately not pure white. A 264px static near-white disc is
     * the worst thing you can leave on an AMOLED, and this app is otherwise
     * all black, so the eye is dimmed and kept drifting (see odd_tick). */
    o->sclera = lv_obj_create(app->root);
    lv_obj_set_size(o->sclera, 264, 264);
    lv_obj_set_style_radius(o->sclera, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->sclera, lv_color_hex(0xC9C5BA), 0);
    lv_obj_set_style_border_width(o->sclera, 0, 0);
    lv_obj_set_style_shadow_width(o->sclera, 0, 0);
    lv_obj_clear_flag(o->sclera, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(o->sclera, LV_ALIGN_CENTER, 0, -22);
    lv_obj_add_flag(o->sclera, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o->sclera, eye_tap, LV_EVENT_CLICKED, o);

    /* iris (moves) */
    o->iris = lv_obj_create(o->sclera);
    lv_obj_set_size(o->iris, 120, 120);
    lv_obj_set_style_radius(o->iris, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->iris, lv_color_hex(o->accent), 0);
    lv_obj_set_style_border_width(o->iris, 0, 0);
    lv_obj_clear_flag(o->iris, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(o->iris);

    /* pupil */
    o->pupil = lv_obj_create(o->iris);
    lv_obj_set_size(o->pupil, 54, 54);
    lv_obj_set_style_radius(o->pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->pupil, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_width(o->pupil, 0, 0);
    lv_obj_center(o->pupil);

    /* eyelid — a black disc we drop over the eye to blink */
    o->lid = lv_obj_create(app->root);
    lv_obj_set_size(o->lid, 268, 268);
    lv_obj_set_style_radius(o->lid, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->lid, lv_color_hex(CHAOS_COL_BG), 0);
    lv_obj_set_style_border_width(o->lid, 0, 0);
    lv_obj_align(o->lid, LV_ALIGN_CENTER, 0, -22);
    lv_obj_add_flag(o->lid, LV_OBJ_FLAG_HIDDEN);

    o->speech = lv_label_create(app->root);
    lv_obj_set_width(o->speech, 360);
    lv_label_set_long_mode(o->speech, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(o->speech, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(o->speech, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_set_style_text_font(o->speech, &lv_font_montserrat_20, 0);
    lv_obj_align(o->speech, LV_ALIGN_CENTER, 0, 150);

    o->meta = lv_label_create(app->root);
    lv_obj_set_style_text_color(o->meta, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_text_font(o->meta, &lv_font_montserrat_14, 0);
    lv_obj_align(o->meta, LV_ALIGN_TOP_MID, 0, 40);
    update_meta(o);

    /* greeting keyed on history */
    int64_t gap = (last_seen > 0) ? (time(NULL) - last_seen) : 0;
    if (o->encounters <= 1)        say(o, "oh. a new one.");
    else if (gap > 6 * 3600)       say(o, "back again. it has been a while.");
    else                           say(o, "you again. hello, hello.");

    odd_save(o);
}

static void odd_tick(chaos_app_t *app, uint32_t dt)
{
    odd_t *o = app->user;

    /* A slow wander layered on top of the tilt target. It keeps the eye alive
     * when the IMU is quiet, and — more importantly — stops a big bright disc
     * from sitting on the same pixels indefinitely. */
    o->idle_phase += dt * 0.0006f;
    float wander_x = sinf(o->idle_phase) * 12.0f;
    float wander_y = cosf(o->idle_phase * 0.73f) * 9.0f;

    /* ease pupil toward where the tilt is pointing */
    o->cur_x += ((o->tgt_x + wander_x) - o->cur_x) * 0.18f;
    o->cur_y += ((o->tgt_y + wander_y) - o->cur_y) * 0.18f;
    lv_obj_align(o->iris, LV_ALIGN_CENTER, (int)o->cur_x, (int)o->cur_y);

    /* Shift the whole eye a few pixels on a slower cycle — standard OLED
     * pixel-shifting, too small to notice but enough to spread the load. */
    int shift_x = (int)(sinf(o->idle_phase * 0.31f) * 4.0f);
    int shift_y = (int)(cosf(o->idle_phase * 0.27f) * 4.0f);
    lv_obj_align(o->sclera, LV_ALIGN_CENTER, shift_x, -22 + shift_y);
    lv_obj_align(o->lid,    LV_ALIGN_CENTER, shift_x, -22 + shift_y);

    /* agitation cools; dilation relaxes */
    o->agit  -= dt * 0.00007f;   if (o->agit < 0) o->agit = 0;
    o->dilate += (1.0f - o->dilate) * 0.06f;

    int psz = (int)(54 * o->dilate);
    lv_obj_set_size(o->pupil, psz, psz);
    lv_obj_center(o->pupil);
    lv_obj_set_style_bg_color(o->iris,
        lv_color_hex(mix(o->accent, CHAOS_COL_DANGER, o->agit)), 0);

    /* blinking */
    o->blink_ms += dt;
    if (o->blink_left_ms > 0) {
        o->blink_left_ms = (o->blink_left_ms > dt) ? o->blink_left_ms - dt : 0;
        lv_obj_clear_flag(o->lid, LV_OBJ_FLAG_HIDDEN);
        if (o->blink_left_ms == 0) lv_obj_add_flag(o->lid, LV_OBJ_FLAG_HIDDEN);
    } else if (o->blink_ms >= o->next_blink_ms) {
        o->blink_ms = 0;
        o->next_blink_ms = 1800 + (esp_random() % 3500);
        o->blink_left_ms = 120;
    }

    /* idle mutterings */
    o->phrase_ms += dt;
    if (o->phrase_ms >= 5200) {
        o->phrase_ms = 0;
        say(o, o->agit > 0.5f ? pick(AGIT, 4) : pick(CALM, 6));
    }
}

static void odd_event(chaos_app_t *app, const chaos_event_t *ev)
{
    odd_t *o = app->user;
    switch (ev->type) {
    case CHAOS_EV_TILT:
        /* roll -> horizontal gaze, pitch -> vertical gaze */
        o->tgt_x = clampf( ev->vec.x * 1.3f, -58.0f, 58.0f);
        o->tgt_y = clampf(-ev->vec.y * 1.3f, -58.0f, 58.0f);
        break;
    case CHAOS_EV_SHAKE:
        o->agit = 1.0f;
        o->dilate = 1.6f;
        o->blink_left_ms = 140;
        o->shakes++;
        say(o, pick(STARTLED, 4));
        update_meta(o);
        odd_save(o);
        break;
    case CHAOS_EV_FLIP:
        if (ev->i) say(o, "everything is upside down now. rude.");
        break;
    default:
        break;
    }
}

static void odd_close(chaos_app_t *app)
{
    odd_t *o = app->user;
    if (o) { odd_save(o); free(o); }
}

const chaos_app_desc_t app_companion_desc = {
    .id = "oddball", .name = "Oddball", .glyph = "0o", .accent = 0x9B8CFF,
    .on_open = odd_open, .on_close = odd_close,
    .on_tick = odd_tick, .on_event = odd_event,
};
