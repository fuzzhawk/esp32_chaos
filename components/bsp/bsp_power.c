/*
 * bsp_power.c — AXP2101 PMIC, best-effort battery telemetry.
 *
 * The AXP2101 exposes a built-in fuel gauge (register 0xA4 = state-of-charge
 * percent) once battery detection + the coulomb counter are enabled, which is
 * the board's default power-on state. We read that plus the charge/VBUS status
 * bits. Voltage is estimated from SoC — good enough for a battery icon; wire up
 * the VBAT ADC registers if you need millivolt accuracy.
 */
#include "bsp.h"
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_power";

#define AXP_STATUS1   0x00   /* bit5: VBUS good           */
#define AXP_STATUS2   0x01   /* bits[6:5]: charge state   */
#define AXP_INTEN1    0x40   /* interrupt enables 1..3    */
#define AXP_INTSTS1   0x48   /* interrupt status 1..3, write 1 to clear */
#define AXP_GAUGE_SOC 0xA4   /* battery percentage 0..100 */

/* PKEY_SHORT is bit 11 of the 24-bit IRQ word, i.e. bit 3 of INTSTS2;
 * PKEY_LONG is bit 10, i.e. bit 2. */
#define AXP_INT2_PKEY_SHORT  (1 << 3)
#define AXP_INT2_PKEY_LONG   (1 << 2)

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t bsp_power_init(void)
{
    if (!bsp_i2c_present(BSP_AXP2101_I2C_ADDR)) {
        ESP_LOGW(TAG, "no AXP2101 at 0x%02X — battery reporting disabled",
                 BSP_AXP2101_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_AXP2101_I2C_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bsp_i2c_bus(), &dev_cfg, &s_dev),
                        TAG, "add pmic device");

    /* Enable the power-key interrupts and clear anything stale, so the key
     * can be polled as an ordinary button. */
    wr(AXP_INTEN1 + 1, AXP_INT2_PKEY_SHORT | AXP_INT2_PKEY_LONG);
    for (int i = 0; i < 3; i++) wr(AXP_INTSTS1 + i, 0xFF);

    ESP_LOGI(TAG, "AXP2101 ready");
    return ESP_OK;
}

bool bsp_power_pwrkey_pressed(void)
{
    if (!s_dev) return false;

    uint8_t sts = 0;
    if (rd(AXP_INTSTS1 + 1, &sts) != ESP_OK) return false;
    if (!(sts & AXP_INT2_PKEY_SHORT)) return false;

    wr(AXP_INTSTS1 + 1, AXP_INT2_PKEY_SHORT);   /* write 1 to clear */
    return true;
}

esp_err_t bsp_power_status(bsp_power_status_t *out)
{
    if (!out)   return ESP_ERR_INVALID_ARG;
    if (!s_dev) return ESP_ERR_INVALID_STATE;   /* absent: no bus traffic */

    uint8_t soc = 0, s1 = 0, s2 = 0;
    rd(AXP_GAUGE_SOC, &soc);
    rd(AXP_STATUS1, &s1);
    rd(AXP_STATUS2, &s2);

    if (soc > 100) soc = 100;
    out->percent  = soc;
    out->on_usb   = (s1 & 0x20) != 0;
    out->charging = ((s2 >> 5) & 0x03) == 0x01;
    out->voltage  = 3.3f + (soc / 100.0f) * 0.9f;   /* ~3.3–4.2 V estimate */
    return ESP_OK;
}
