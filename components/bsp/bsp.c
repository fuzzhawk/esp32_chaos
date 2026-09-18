/*
 * bsp.c — board bring-up, framebuffer and input.
 *
 * Boot order: I2C -> bus scan -> PMIC -> AMOLED -> touch -> IMU -> buttons.
 * After bsp_init() the rest of the system just draws into bsp_fb() and calls
 * bsp_present(). No display toolkit is involved.
 */
#include "bsp.h"
#include "bsp_priv.h"

#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "bsp";

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t    s_panel;
static uint16_t                 *s_fb;
static SemaphoreHandle_t         s_flush_done;

/* Bring-up results, reported periodically by status_task(). */
static bool s_pmic_ok, s_touch_ok, s_imu_ok, s_disp_ok;
static int  s_i2c_count;
static char s_i2c_list[64];

/* Button edges, set by button_task() and consumed by bsp_button_pressed(). */
static volatile bool s_btn_hit[BSP_BTN_COUNT];

/* ------------------------------------------------------------------ */
/*  Framebuffer                                                        */
/* ------------------------------------------------------------------ */

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *ev,
                                          void *ctx)
{
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &hp_woken);
    return hp_woken == pdTRUE;
}

uint16_t *bsp_fb(void) { return s_fb; }

void bsp_present(void)
{
    if (!s_panel || !s_fb) return;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_FB_W, BSP_FB_H, s_fb);
    /* Block until the DMA has actually shipped the buffer; the caller reuses
     * (and reads back from) this same memory on the next frame. */
    xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(1000));
}

/* ------------------------------------------------------------------ */
/*  Input                                                              */
/* ------------------------------------------------------------------ */

bool bsp_button_pressed(bsp_btn_t btn)
{
    if (btn >= BSP_BTN_COUNT) return false;
    if (!s_btn_hit[btn]) return false;
    s_btn_hit[btn] = false;
    return true;
}

static void button_task(void *arg)
{
    (void)arg;
    bool boot_was_down = false;

    for (;;) {
        /* BOOT: active low, simple debounce by sampling at 25ms. */
        bool boot_down = gpio_get_level(BSP_BTN_BOOT_GPIO) == 0;
        if (boot_down && !boot_was_down) {
            s_btn_hit[BSP_BTN_ACTION] = true;
            ESP_LOGI(TAG, "button: ACTION (BOOT)");
        }
        boot_was_down = boot_down;

        /* Power key: the PMIC latches the press for us. */
        if (bsp_power_pwrkey_pressed()) {
            s_btn_hit[BSP_BTN_MODE] = true;
            ESP_LOGI(TAG, "button: MODE (power key)");
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

bool bsp_imu_gravity(float *out_gx, float *out_gy)
{
    bsp_imu_sample_t s;
    if (bsp_imu_read(&s) != ESP_OK || !s.valid) {
        *out_gx = 0.0f;
        *out_gy = 1.0f;          /* sane default: straight down */
        return false;
    }

    float gx = s.ax, gy = s.ay;
#if BSP_IMU_SCREEN_SWAP_XY
    { float t = gx; gx = gy; gy = t; }
#endif
#if BSP_IMU_SCREEN_INVERT_X
    gx = -gx;
#endif
#if BSP_IMU_SCREEN_INVERT_Y
    gy = -gy;
#endif

    /* Normalise the in-plane component; if the board is flat there is no
     * meaningful direction, so fall back to down. */
    float mag = sqrtf(gx * gx + gy * gy);
    if (mag < 0.12f) {
        *out_gx = 0.0f;
        *out_gy = 1.0f;
        return true;
    }
    *out_gx = gx / mag;
    *out_gy = gy / mag;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Status                                                             */
/* ------------------------------------------------------------------ */

/* The USB console re-enumerates at app start, so the boot log can never be
 * caught reliably. Repeat the peripheral summary instead. */
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
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ------------------------------------------------------------------ */
/*  Bring-up                                                           */
/* ------------------------------------------------------------------ */

esp_err_t bsp_init(void)
{
    ESP_LOGI(TAG, "chaosOS board bring-up");

    /* The I2C driver logs an ERROR per failed transaction, which drowns the
     * console if a chip is missing; our own layers report what matters. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");
    s_i2c_count = bsp_i2c_scan(s_i2c_list, sizeof(s_i2c_list));

    s_pmic_ok = (bsp_power_init() == ESP_OK);
    if (!s_pmic_ok) ESP_LOGW(TAG, "continuing without PMIC telemetry");

    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done, ESP_ERR_NO_MEM, TAG, "flush sem");

    ESP_RETURN_ON_ERROR(bsp_display_init(&s_panel_io, &s_panel,
                                         on_color_trans_done, NULL),
                        TAG, "display");
    s_disp_ok = true;

    s_fb = heap_caps_malloc(BSP_FB_PX * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_fb, ESP_ERR_NO_MEM, TAG, "framebuffer");
    memset(s_fb, 0, BSP_FB_PX * sizeof(uint16_t));

    s_touch_ok = (bsp_touch_init() == ESP_OK);
    if (!s_touch_ok) ESP_LOGW(TAG, "continuing without touch");

    s_imu_ok = (bsp_imu_init() == ESP_OK);
    if (!s_imu_ok) ESP_LOGW(TAG, "continuing without IMU");

    const gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ULL << BSP_BTN_BOOT_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot_cfg);

    bsp_set_brightness(90);
    xTaskCreatePinnedToCore(button_task, "bsp_btn", 3072, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(status_task, "bsp_status", 3072, NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "board ready");
    return ESP_OK;
}

esp_err_t bsp_set_brightness(uint8_t percent)
{
    return bsp_display_set_brightness(s_panel_io, percent);
}
