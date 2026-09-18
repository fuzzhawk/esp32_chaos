/*
 * bsp.c — top-level board bring-up and LVGL integration.
 *
 * Boot order: PMIC → I2C → AMOLED → LVGL port → touch → IMU. esp_lvgl_port
 * runs LVGL on its own task with a recursive mutex; bsp_lvgl_lock/unlock are
 * thin wrappers so the rest of chaosOS can safely build UI from any task.
 */
#include "bsp.h"
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp";

static lv_disp_t                 *s_disp;
static esp_lcd_panel_io_handle_t  s_panel_io;
static lv_indev_drv_t             s_indev_drv;

/* Bring-up results, reported periodically by status_task(). */
static bool s_pmic_ok, s_touch_ok, s_imu_ok, s_disp_ok;
static int  s_i2c_count;
static char s_i2c_list[64];

/* The USB console re-enumerates at app start, so whatever `screen` was showing
 * during boot is lost and the boot banner can never be caught reliably. Repeat
 * a one-line summary forever instead: attach whenever you like and the state of
 * every peripheral is on screen within five seconds. */
static void status_task(void *arg)
{
    (void)arg;
    for (;;) {
        ESP_LOGI(TAG, "status: display=%s  i2c=%d [%s]  touch=%s  imu=%s  pmic=%s",
                 s_disp_ok ? "on" : "FAILED",
                 s_i2c_count, s_i2c_list,
                 s_touch_ok ? "ok" : "absent",
                 s_imu_ok   ? "ok" : "absent",
                 s_pmic_ok  ? "ok" : "absent");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* LVGL polls this; it runs on the LVGL task so the blocking I2C read is fine
 * (a 15-byte transfer at 400kHz is well under a millisecond). LVGL needs the
 * last known coordinates on release, so they are held between presses. */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static uint16_t last_x, last_y;
    uint16_t x, y;

    if (bsp_touch_get_point(&x, &y)) {
        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

esp_err_t bsp_init(void)
{
    ESP_LOGI(TAG, "chaosOS board bring-up");

    /* The I2C driver logs an ERROR line for every failed transaction, which
     * drowns the console if a chip is missing. Our own layers report what
     * matters (see the bus scan below), so silence the driver's chatter. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");
    s_i2c_count = bsp_i2c_scan(s_i2c_list, sizeof(s_i2c_list));

    s_pmic_ok = (bsp_power_init() == ESP_OK);
    if (!s_pmic_ok) {
        ESP_LOGW(TAG, "continuing without PMIC telemetry");
    }

    /* --- display --- */
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_init(&s_panel_io, &panel), TAG, "display");

    /* --- LVGL port --- */
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = s_panel_io,
        .panel_handle  = panel,
        .buffer_size   = BSP_LCD_H_RES * 80,   /* partial buffers, in PSRAM */
        .double_buffer = true,
        .hres          = BSP_LCD_H_RES,
        .vres          = BSP_LCD_V_RES,
        .flags = {
            .buff_spiram = true,   /* framebuffers live in PSRAM */
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return ESP_FAIL;
    }

    /* --- touch ---
     * Registered as a plain LVGL pointer device rather than through
     * lvgl_port_add_touch(), which requires an esp_lcd_touch handle; the
     * CST9217 has no esp_lcd_touch driver so we poll it ourselves. */
    if (bsp_touch_init() == ESP_OK) {
        s_touch_ok = true;
        lvgl_port_lock(0);
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
        s_indev_drv.disp    = s_disp;
        s_indev_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_indev_drv);
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "continuing without touch");
    }
    s_disp_ok = true;

    /* --- IMU (non-fatal; the OS just loses motion events) --- */
    s_imu_ok = (bsp_imu_init() == ESP_OK);
    if (!s_imu_ok) {
        ESP_LOGW(TAG, "continuing without IMU");
    }

    bsp_set_brightness(90);
    xTaskCreate(status_task, "bsp_status", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "board ready");
    return ESP_OK;
}

lv_disp_t *bsp_display(void) { return s_disp; }

bool bsp_lvgl_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)              { lvgl_port_unlock(); }

esp_err_t bsp_set_brightness(uint8_t percent)
{
    return bsp_display_set_brightness(s_panel_io, percent);
}
