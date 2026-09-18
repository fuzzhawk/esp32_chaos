/*
 * bsp_touch.c — CST9217 capacitive touch driver.
 *
 * The CST92xx family speaks a small request/ack protocol rather than a plain
 * register map:
 *   1. write the 16-bit read command 0xD000
 *   2. read back a 15-byte report
 *   3. write 0xD000 + 0xAB to acknowledge it
 * Byte 6 of the report must echo 0xAB or the frame is stale. Finger 0 lives at
 * bytes 0..4; byte 5 holds the touch count; a second finger (which we ignore)
 * starts at byte 7.
 *
 * Protocol reference: Waveshare's own sample code for this board
 * (SensorLib TouchDrvCST92xx). The chip's IRQ line only pulses periodically
 * rather than tracking press state, so we poll instead of using it.
 */
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_touch";

#define CST_READ_CMD_H   0xD0
#define CST_READ_CMD_L   0x00
#define CST_ACK          0xAB
#define CST_MAX_FINGERS  2
#define CST_REPORT_LEN   (CST_MAX_FINGERS * 5 + 5)   /* 15 bytes */
#define CST_EVT_DOWN     0x06

static i2c_master_dev_handle_t s_dev;

esp_err_t bsp_touch_init(void)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_TOUCH_I2C_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bsp_i2c_bus(), &dev_cfg, &s_dev),
                        TAG, "add touch device");

#if BSP_TOUCH_RST_GPIO >= 0
    const gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << BSP_TOUCH_RST_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "touch rst cfg");
    gpio_set_level(BSP_TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BSP_TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));      /* controller boot time */
#endif

    /* Probe after reset — the CST9217 needs the reset pulse before it ACKs. */
    if (!bsp_i2c_present(BSP_TOUCH_I2C_ADDR)) {
        ESP_LOGW(TAG, "no CST9217 at 0x%02X — touch disabled",
                 BSP_TOUCH_I2C_ADDR);
        s_dev = NULL;                   /* polling becomes a no-op */
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "CST9217 ready at 0x%02X", BSP_TOUCH_I2C_ADDR);
    return ESP_OK;
}

bool bsp_touch_get_point(uint16_t *out_x, uint16_t *out_y)
{
    if (!s_dev) return false;

    const uint8_t req[2] = { CST_READ_CMD_H, CST_READ_CMD_L };
    uint8_t rx[CST_REPORT_LEN] = {0};

    if (i2c_master_transmit_receive(s_dev, req, sizeof(req), rx, sizeof(rx), 50) != ESP_OK) {
        return false;
    }

    /* Acknowledge the report so the controller can produce the next one. */
    const uint8_t ack[3] = { CST_READ_CMD_H, CST_READ_CMD_L, CST_ACK };
    i2c_master_transmit(s_dev, ack, sizeof(ack), 50);

    if (rx[6] != CST_ACK) {
        return false;                    /* stale / incomplete frame */
    }

    uint8_t points = rx[5] & 0x7F;
    if (points == 0 || points > CST_MAX_FINGERS) {
        return false;
    }

    /* Finger 0 only — chaosOS is a single-touch UI. */
    const uint8_t evt = rx[0] & 0x0F;
    if (evt != CST_EVT_DOWN) {
        return false;                    /* finger lifted */
    }

    uint16_t x = (uint16_t)((rx[1] << 4) | (rx[3] >> 4));
    uint16_t y = (uint16_t)((rx[2] << 4) | (rx[3] & 0x0F));

    if (x >= BSP_LCD_H_RES) x = BSP_LCD_H_RES - 1;
    if (y >= BSP_LCD_V_RES) y = BSP_LCD_V_RES - 1;

    *out_x = x;
    *out_y = y;
    return true;
}
