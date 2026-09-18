/*
 * bsp.h — board-support API for chaosOS.
 *
 * There is no UI toolkit here. The board hands you one RGB565 framebuffer,
 * a touch point, a gravity vector and two buttons; everything above draws
 * pixels. LVGL was removed deliberately: every mode is raw-pixel, and the
 * glitch mode needs direct framebuffer access anyway.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bsp_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_FB_W  BSP_LCD_H_RES
#define BSP_FB_H  BSP_LCD_V_RES
#define BSP_FB_PX (BSP_FB_W * BSP_FB_H)

/* Bring up the whole board: PMIC -> I2C -> AMOLED -> touch -> IMU -> buttons.
 * On success bsp_fb() is valid and bsp_present() will push it to the glass. */
esp_err_t bsp_init(void);

/* The framebuffer: BSP_FB_W * BSP_FB_H pixels, RGB565, byte-swapped for the
 * panel (build colours with fx_rgb(), never a raw 0x1234 literal).
 * It persists between frames, so feedback effects can read what they drew. */
uint16_t *bsp_fb(void);

/* Push the framebuffer to the panel and block until the transfer completes. */
void bsp_present(void);

/* Panel brightness, 0..100 (%). */
esp_err_t bsp_set_brightness(uint8_t percent);

/* ---- input ---------------------------------------------------------- */

/* True while a finger is down; fills screen coordinates. */
bool bsp_touch_get_point(uint16_t *x, uint16_t *y);

typedef enum {
    BSP_BTN_MODE = 0,   /* AXP2101 power key: cycles modes   */
    BSP_BTN_ACTION,     /* BOOT (GPIO0): the mode's own verb */
    BSP_BTN_COUNT,
} bsp_btn_t;

/* Edge-triggered and self-clearing: returns true once per press. */
bool bsp_button_pressed(bsp_btn_t btn);

/* ---- sensors -------------------------------------------------------- */

typedef struct {
    float ax, ay, az;   /* acceleration, g     */
    float gx, gy, gz;   /* angular rate, deg/s */
    float temp_c;
    bool  valid;
} bsp_imu_sample_t;

esp_err_t bsp_imu_read(bsp_imu_sample_t *out);

/* Gravity mapped into screen space: +x right, +y down, roughly -1..1 each.
 * Returns false (and a straight-down vector) when no IMU is present. */
bool bsp_imu_gravity(float *out_gx, float *out_gy);

typedef struct {
    float   voltage;
    uint8_t percent;
    bool    charging;
    bool    on_usb;
} bsp_power_status_t;

esp_err_t bsp_power_status(bsp_power_status_t *out);

#ifdef __cplusplus
}
#endif
