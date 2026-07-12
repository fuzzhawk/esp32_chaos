/*
 * chaos_app.h — the app contract.
 *
 * An app is a static descriptor plus a handful of callbacks. The OS creates a
 * full-screen container (app->root) before on_open and destroys it after
 * on_close, so apps never manage their own screen or worry about the launcher.
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"
#include "chaos_event.h"

typedef struct chaos_app chaos_app_t;

typedef struct {
    const char *id;      /* stable unique key, e.g. "life"         */
    const char *name;    /* shown on the launcher tile             */
    const char *glyph;   /* short label/symbol drawn on the tile   */
    uint32_t    accent;  /* 0xRRGGBB theme colour for this app     */

    void (*on_open)(chaos_app_t *app);                       /* build UI into app->root */
    void (*on_close)(chaos_app_t *app);                      /* free app->user          */
    void (*on_tick)(chaos_app_t *app, uint32_t dt_ms);       /* ~30 Hz while active     */
    void (*on_event)(chaos_app_t *app, const chaos_event_t *ev); /* sensor/system events */
} chaos_app_desc_t;

struct chaos_app {
    const chaos_app_desc_t *desc;
    lv_obj_t               *root;   /* OS-owned full-screen container */
    void                   *user;   /* app-private state              */
};
