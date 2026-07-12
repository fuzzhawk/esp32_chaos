/* bsp_priv.h — cross-file plumbing internal to the bsp component. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

/* The one shared I2C bus (touch + IMU + PMIC live here). */
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_bus(void);

/* AMOLED panel bring-up (QSPI + CO5300 init). Fills the two handles. */
esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *out_io,
                           esp_lcd_panel_handle_t *out_panel);
esp_err_t bsp_display_set_brightness(esp_lcd_panel_io_handle_t io, uint8_t percent);

/* Touch bring-up; the returned handle is handed to esp_lvgl_port. */
esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *out_touch);

/* Sensors / power (own their I2C device handles internally). */
esp_err_t bsp_imu_init(void);
esp_err_t bsp_power_init(void);
