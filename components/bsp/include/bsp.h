/*
 * bsp.h — public board-support API for chaosOS.
 *
 * The BSP owns all hardware: power, the shared I2C bus, the AMOLED panel
 * (wired into LVGL through esp_lvgl_port), capacitive touch, and the IMU.
 * Everything above this line talks to LVGL objects and bsp_imu_read()
 * and never touches a register.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the whole board and LVGL, in order:
 *   PMIC → I2C bus → AMOLED panel → LVGL port → touch → IMU.
 * On success LVGL is running on its own task and lv_scr_act() is live. */
esp_err_t bsp_init(void);

/* The active LVGL display (already registered with the panel + touch). */
lv_disp_t *bsp_display(void);

/* Take/give the LVGL mutex. ANY LVGL call from outside the LVGL task must
 * be wrapped in these. Returns true if the lock was taken. */
bool bsp_lvgl_lock(uint32_t timeout_ms);
void bsp_lvgl_unlock(void);

/* Screen backlight / AMOLED brightness, 0..100 (%). */
esp_err_t bsp_set_brightness(uint8_t percent);

/* Latest IMU sample in SI-ish units (g for accel, dps for gyro). */
typedef struct {
    float ax, ay, az;   /* acceleration, g       */
    float gx, gy, gz;   /* angular rate, deg/s   */
    float temp_c;       /* die temperature, °C   */
    bool  valid;
} bsp_imu_sample_t;

esp_err_t bsp_imu_read(bsp_imu_sample_t *out);

/* Battery / charge state from the AXP2101. */
typedef struct {
    float   voltage;    /* volts */
    uint8_t percent;    /* 0..100, best-effort estimate */
    bool    charging;
    bool    on_usb;
} bsp_power_status_t;

esp_err_t bsp_power_status(bsp_power_status_t *out);

#ifdef __cplusplus
}
#endif
