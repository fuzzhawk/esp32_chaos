/*
 * bsp_touch.c — capacitive touch bring-up.
 *
 * Defaults to an FT-series controller (FT3168 is register-compatible with
 * the ft5x06 driver's read path). For a CST9217 board, swap the managed
 * component in idf_component.yml and the *_new_i2c_* call below.
 */
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_touch";

esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    const esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();

    esp_lcd_panel_io_handle_t tp_io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(bsp_i2c_bus(), &io_cfg, &tp_io),
        TAG, "touch panel io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,
        .y_max        = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags  = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };

    esp_err_t err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, out_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed: %s (check controller/addr in bsp_pins.h)",
                 esp_err_to_name(err));
    }
    return err;
}
