/* bsp_i2c.c — the single shared I2C master bus. */
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include <stdio.h>
static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = BSP_I2C_PORT,
        .sda_io_num                   = BSP_I2C_SDA_GPIO,
        .scl_io_num                   = BSP_I2C_SCL_GPIO,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    }
    return err;
}

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    return s_bus;
}

bool bsp_i2c_present(uint8_t addr)
{
    /* Short timeout on purpose: a present chip ACKs in well under a
     * millisecond, and a full 0x08..0x77 sweep at 100ms/address would stall
     * boot for 11 seconds when the bus is dead. */
    return s_bus && i2c_master_probe(s_bus, addr, 10) == ESP_OK;
}

/* Walk the bus and report every device that ACKs. This is the single most
 * useful thing in the log when a peripheral goes quiet: it separates "the bus
 * is dead" from "one chip is at an unexpected address".
 * Writes a space-separated address list into `out`; returns the count. */
int bsp_i2c_scan(char *out, size_t len)
{
    ESP_LOGI(TAG, "scanning I2C bus (SDA=%d SCL=%d @ %d Hz)",
             BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO, BSP_I2C_FREQ_HZ);

    int found = 0;
    size_t used = 0;
    if (out && len) out[0] = '\0';

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (!bsp_i2c_present(addr)) continue;

        const char *who = (addr == BSP_TOUCH_I2C_ADDR)   ? " (CST9217 touch)"
                        : (addr == BSP_IMU_I2C_ADDR)     ? " (QMI8658 IMU)"
                        : (addr == BSP_AXP2101_I2C_ADDR) ? " (AXP2101 PMIC)"
                        : "";
        ESP_LOGI(TAG, "  found device at 0x%02X%s", addr, who);
        found++;
        if (out && used + 6 < len) {
            used += snprintf(out + used, len - used, "%s0x%02X",
                             used ? " " : "", addr);
        }
    }

    if (found == 0) {
        ESP_LOGE(TAG, "  NO DEVICES ON THE BUS — wrong SDA/SCL pins, or the bus");
        ESP_LOGE(TAG, "  is held low / unpowered. Nothing on I2C will work.");
    } else {
        ESP_LOGI(TAG, "  %d device(s) responded", found);
    }
    return found;
}
