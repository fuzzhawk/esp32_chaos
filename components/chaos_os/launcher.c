/*
 * launcher.c — the chaosOS home screen.
 *
 * A vertically scrolling column of app tiles, centred so nothing lands in the
 * round display's clipped corners. Tapping a tile launches its app.
 */
#include "chaos_internal.h"
#include "chaos_os.h"
#include "theme.h"

static void tile_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    chaos_os_launch(id);
}

static lv_obj_t *make_tile(lv_obj_t *parent, const chaos_app_desc_t *app)
{
    lv_obj_t *tile = theme_card(parent);
    lv_obj_set_size(tile, 320, 96);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile, 16, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, (void *)app->id);

    theme_badge(tile, app->glyph, app->accent, 64);

    lv_obj_t *col = lv_obj_create(tile);
    lv_obj_remove_style_all(col);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);

    lv_obj_t *name = lv_label_create(col);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_color(name, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0);

    lv_obj_t *tap = lv_label_create(col);
    lv_label_set_text(tap, "tap to open");
    lv_obj_set_style_text_color(tap, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_text_font(tap, &lv_font_montserrat_14, 0);
    return tile;
}

lv_obj_t *chaos_launcher_build(lv_obj_t *parent,
                               const chaos_app_desc_t *const *apps, int count)
{
    lv_obj_t *page = lv_obj_create(parent);
    theme_screen(page);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page, 14, 0);
    lv_obj_set_style_pad_top(page, 64, 0);
    lv_obj_set_style_pad_bottom(page, 80, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "chaosOS");
    lv_obj_set_style_text_color(title, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);

    lv_obj_t *sub = lv_label_create(page);
    lv_label_set_text(sub, "an experiment");
    lv_obj_set_style_text_color(sub, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);

    for (int i = 0; i < count; i++) {
        make_tile(page, apps[i]);
    }
    return page;
}
