/* bsp_priv.h — cross-file plumbing internal to the bsp component. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"

/* The one shared I2C bus (touch + IMU + PMIC live here). */
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_bus(void);
bool bsp_i2c_present(uint8_t addr);
int  bsp_i2c_scan(char *out, size_t len);

/* AMOLED panel bring-up. `done_cb` fires when a draw_bitmap transfer lands. */
esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *out_io,
                           esp_lcd_panel_handle_t *out_panel,
                           esp_lcd_panel_io_color_trans_done_cb_t done_cb,
                           void *cb_ctx);
esp_err_t bsp_display_set_brightness(esp_lcd_panel_io_handle_t io, uint8_t percent);

/* CST9217 touch. */
esp_err_t bsp_touch_init(void);

/* Sensors / power. */
esp_err_t bsp_imu_init(void);
esp_err_t bsp_power_init(void);

/* AXP2101 power key: true once per short press (self-clearing). */
bool bsp_power_pwrkey_pressed(void);
