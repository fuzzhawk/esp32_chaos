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
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp";

static lv_disp_t                 *s_disp;
static esp_lcd_panel_io_handle_t  s_panel_io;

esp_err_t bsp_init(void)
{
    ESP_LOGI(TAG, "chaosOS board bring-up");

    ESP_RETURN_ON_ERROR(bsp_i2c_init(),   TAG, "i2c");
    ESP_RETURN_ON_ERROR(bsp_power_init(), TAG, "power");

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

    /* --- touch --- */
    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_init(&touch) == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = s_disp, .handle = touch };
        lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGW(TAG, "continuing without touch");
    }

    /* --- IMU (non-fatal; the OS just loses motion events) --- */
    if (bsp_imu_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without IMU");
    }

    bsp_set_brightness(90);
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
