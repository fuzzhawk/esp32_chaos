/*
 * bsp_imu.c — minimal QMI8658 6-axis IMU driver.
 *
 * Configured for ±8 g accel and ±512 dps gyro. That's plenty to detect
 * shakes, tilts and taps, which is all chaosOS asks of it. The heavy
 * lifting (gesture detection) lives up in the OS sensor task.
 */
#include "bsp.h"
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_imu";

#define QMI_WHO_AM_I  0x00
#define QMI_CTRL1     0x02
#define QMI_CTRL2     0x03   /* accel: full-scale + ODR */
#define QMI_CTRL3     0x04   /* gyro:  full-scale + ODR */
#define QMI_CTRL7     0x08   /* enable accel/gyro       */
#define QMI_TEMP_L    0x33
#define QMI_AX_L      0x35   /* AX,AY,AZ,GX,GY,GZ little-endian, 12 bytes */

#define ACCEL_LSB_PER_G    4096.0f   /* ±8 g  over 16 bit */
#define GYRO_LSB_PER_DPS     64.0f   /* ±512 dps over 16 bit */

static i2c_master_dev_handle_t s_dev;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *dst, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, dst, len, 100);
}

esp_err_t bsp_imu_init(void)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_IMU_I2C_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bsp_i2c_bus(), &dev_cfg, &s_dev),
                        TAG, "add imu device");

    uint8_t who = 0;
    if (rd(QMI_WHO_AM_I, &who, 1) == ESP_OK && who != 0x05) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I=0x%02x (expected 0x05) — check addr/wiring", who);
    }

    ESP_RETURN_ON_ERROR(wr(QMI_CTRL1, 0x40), TAG, "ctrl1"); /* addr auto-increment, LE */
    ESP_RETURN_ON_ERROR(wr(QMI_CTRL2, 0x24), TAG, "ctrl2"); /* ±8 g,  ~500 Hz */
    ESP_RETURN_ON_ERROR(wr(QMI_CTRL3, 0x54), TAG, "ctrl3"); /* ±512 dps, ~500 Hz */
    ESP_RETURN_ON_ERROR(wr(QMI_CTRL7, 0x03), TAG, "ctrl7"); /* enable accel + gyro */
    ESP_LOGI(TAG, "QMI8658 online");
    return ESP_OK;
}

esp_err_t bsp_imu_read(bsp_imu_sample_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    out->valid = false;

    uint8_t t[2];
    int16_t raw[6];
    if (rd(QMI_TEMP_L, t, 2) != ESP_OK) return ESP_FAIL;
    if (rd(QMI_AX_L, (uint8_t *)raw, sizeof(raw)) != ESP_OK) return ESP_FAIL;

    out->ax = raw[0] / ACCEL_LSB_PER_G;
    out->ay = raw[1] / ACCEL_LSB_PER_G;
    out->az = raw[2] / ACCEL_LSB_PER_G;
    out->gx = raw[3] / GYRO_LSB_PER_DPS;
    out->gy = raw[4] / GYRO_LSB_PER_DPS;
    out->gz = raw[5] / GYRO_LSB_PER_DPS;
    out->temp_c = (int16_t)((t[1] << 8) | t[0]) / 256.0f;
    out->valid = true;
    return ESP_OK;
}
