/*
 * theme.h — shared visual language for chaosOS.
 *
 * The panel is a 466x466 circle, so the theme leans on true black (free on
 * AMOLED), a neon accent per app, and generous centre-weighted layouts that
 * never push content into the clipped corners.
 */
#pragma once

#include "lvgl.h"

#define CHAOS_SCREEN_DIM   466
#define CHAOS_SCREEN_R     233

/* Core palette (0xRRGGBB). */
#define CHAOS_COL_BG       0x000000
#define CHAOS_COL_PANEL    0x121317
#define CHAOS_COL_TEXT     0xEDEDF2
#define CHAOS_COL_MUTED    0x7A7C88
#define CHAOS_COL_DANGER   0xFF4D5E
#define CHAOS_COL_OK       0x37E0A6

/* Turn a bare object into a full-screen chaosOS surface (black, no scroll). */
void theme_screen(lv_obj_t *obj);

/* A rounded translucent card centred content sits on. */
lv_obj_t *theme_card(lv_obj_t *parent);

/* A pill button. `cb` gets LV_EVENT_CLICKED; `user` is forwarded. */
lv_obj_t *theme_button(lv_obj_t *parent, const char *label, uint32_t accent,
                       lv_event_cb_t cb, void *user);

/* A circular icon-glyph badge in the given accent (used by the launcher). */
lv_obj_t *theme_badge(lv_obj_t *parent, const char *glyph, uint32_t accent,
                      lv_coord_t size);
