/*
 * os_core.c — the chaosOS kernel.
 *
 * Owns the app registry and lifecycle, the ~30 Hz scheduler tick (an lv_timer,
 * so it runs on the LVGL task and may touch UI), the event bus (a FreeRTOS
 * queue), and a sensor task that distils IMU samples into chaos events.
 */
#include "chaos_os.h"
#include "chaos_internal.h"
#include "theme.h"
#include "bsp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "chaos_os";

#define MAX_APPS        12
#define EVENT_QUEUE_LEN 16
#define TICK_MS         33

static const chaos_app_desc_t *s_apps[MAX_APPS];
static int                     s_app_count;

static lv_obj_t   *s_root;        /* the active screen                */
static lv_obj_t   *s_launcher;    /* home grid (shown/hidden)         */
static lv_obj_t   *s_home_btn;    /* floating "back to launcher"      */
static chaos_app_t s_active;      /* {0} => at launcher               */

static QueueHandle_t s_events;
static uint32_t      s_last_tick_ms;

uint32_t chaos_os_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

chaos_app_t *chaos_os_active_app(void)
{
    return s_active.desc ? &s_active : NULL;
}

void chaos_os_register_app(const chaos_app_desc_t *desc)
{
    if (!desc || s_app_count >= MAX_APPS) {
        ESP_LOGW(TAG, "cannot register app '%s'", desc ? desc->name : "?");
        return;
    }
    s_apps[s_app_count++] = desc;
}

void chaos_os_post_event(const chaos_event_t *ev)
{
    if (s_events && ev) {
        xQueueSend(s_events, ev, 0);   /* drop if the bus is backed up */
    }
}

/* --------------------------------------------------------------------- */
/*  App lifecycle                                                        */
/* --------------------------------------------------------------------- */

void chaos_os_go_home(void)
{
    if (!s_active.desc) return;
    if (s_active.desc->on_close) s_active.desc->on_close(&s_active);
    if (s_active.root) lv_obj_del(s_active.root);
    memset(&s_active, 0, sizeof(s_active));

    lv_obj_clear_flag(s_launcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_home_btn, LV_OBJ_FLAG_HIDDEN);
}

bool chaos_os_launch(const char *id)
{
    const chaos_app_desc_t *desc = NULL;
    for (int i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) { desc = s_apps[i]; break; }
    }
    if (!desc) { ESP_LOGW(TAG, "no app '%s'", id); return false; }

    chaos_os_go_home();   /* tear down whatever's open first */

    lv_obj_t *root = lv_obj_create(s_root);
    theme_screen(root);

    s_active.desc = desc;
    s_active.root = root;
    s_active.user = NULL;
    if (desc->on_open) desc->on_open(&s_active);

    lv_obj_add_flag(s_launcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_home_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_home_btn);
    ESP_LOGI(TAG, "launched %s", desc->name);
    return true;
}

static void home_btn_cb(lv_event_t *e)
{
    (void)e;
    chaos_os_go_home();
}

/* --------------------------------------------------------------------- */
/*  Scheduler tick (LVGL task)                                           */
/* --------------------------------------------------------------------- */

static void scheduler_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = chaos_os_millis();
    uint32_t dt  = now - s_last_tick_ms;
    s_last_tick_ms = now;

    chaos_app_t *app = chaos_os_active_app();

    chaos_event_t ev;
    while (xQueueReceive(s_events, &ev, 0) == pdTRUE) {
        if (app && app->desc->on_event) app->desc->on_event(app, &ev);
    }
    if (app && app->desc->on_tick) app->desc->on_tick(app, dt);
}

/* --------------------------------------------------------------------- */
/*  Sensor task — IMU + power → events                                   */
/* --------------------------------------------------------------------- */

static void sensor_task(void *arg)
{
    (void)arg;
    const float SHAKE_JERK_G = 0.65f;
    const uint32_t SHAKE_COOLDOWN_MS = 400;

    float    prev_mag = 1.0f;
    uint32_t last_shake = 0;
    int      flip_state = 0;          /* -1 down, +1 up, 0 unknown */
    int      throttle = 0;

    for (;;) {
        bsp_imu_sample_t s;
        if (bsp_imu_read(&s) == ESP_OK && s.valid) {
            float mag  = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
            float jerk = fabsf(mag - prev_mag);
            prev_mag = mag;
            uint32_t now = chaos_os_millis();

            if (jerk > SHAKE_JERK_G && (now - last_shake) > SHAKE_COOLDOWN_MS) {
                last_shake = now;
                chaos_event_t ev = { .type = CHAOS_EV_SHAKE, .ts_ms = now };
                ev.vec.mag = jerk;
                chaos_os_post_event(&ev);
            }

            /* Tilt at ~10 Hz so the companion can track it smoothly. */
            if (++throttle % 5 == 0) {
                chaos_event_t ev = { .type = CHAOS_EV_TILT, .ts_ms = now };
                ev.vec.x = atan2f(s.ay, s.az) * 57.2958f;                       /* roll  */
                ev.vec.y = atan2f(-s.ax, sqrtf(s.ay * s.ay + s.az * s.az)) * 57.2958f; /* pitch */
                chaos_os_post_event(&ev);
            }

            int flip = (s.az < -0.6f) ? -1 : (s.az > 0.6f ? 1 : 0);
            if (flip && flip != flip_state) {
                flip_state = flip;
                chaos_event_t ev = { .type = CHAOS_EV_FLIP, .i = (flip < 0), .ts_ms = now };
                chaos_os_post_event(&ev);
            }
        }

        /* Battery gossip every ~5 s. */
        if (throttle % 250 == 0) {
            bsp_power_status_t p;
            if (bsp_power_status(&p) == ESP_OK) {
                chaos_event_t ev = { .type = CHAOS_EV_BATTERY, .i = p.percent,
                                     .ts_ms = chaos_os_millis() };
                chaos_os_post_event(&ev);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));    /* 50 Hz */
    }
}

/* --------------------------------------------------------------------- */
/*  Boot                                                                 */
/* --------------------------------------------------------------------- */

static void build_home_button(void)
{
    s_home_btn = lv_btn_create(s_root);
    lv_obj_set_size(s_home_btn, 54, 54);
    lv_obj_set_style_radius(s_home_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_home_btn, lv_color_hex(0x1C1D22), 0);
    lv_obj_set_style_bg_opa(s_home_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_home_btn, 2, 0);
    lv_obj_set_style_border_color(s_home_btn, lv_color_hex(CHAOS_COL_MUTED), 0);
    lv_obj_set_style_shadow_width(s_home_btn, 0, 0);
    lv_obj_align(s_home_btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(s_home_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_home_btn, home_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(s_home_btn);
    lv_label_set_text(lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CHAOS_COL_TEXT), 0);
    lv_obj_center(lbl);
}

void chaos_os_start(void)
{
    bsp_lvgl_lock(0);

    s_root = lv_scr_act();
    theme_screen(s_root);

    s_launcher = chaos_launcher_build(s_root, s_apps, s_app_count);
    build_home_button();

    s_events = xQueueCreate(EVENT_QUEUE_LEN, sizeof(chaos_event_t));
    s_last_tick_ms = chaos_os_millis();
    lv_timer_create(scheduler_cb, TICK_MS, NULL);

    bsp_lvgl_unlock();

    xTaskCreatePinnedToCore(sensor_task, "chaos_sensor", 4096, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "chaosOS up with %d apps", s_app_count);
}
