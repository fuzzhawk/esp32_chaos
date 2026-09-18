/*
 * bsp_display.c — 466x466 AMOLED bring-up over QSPI.
 *
 * The panel controller on this board is a CO5300. Espressif's esp_lcd_sh8601
 * driver speaks the same QSPI/DCS protocol, so we reuse it and feed it a
 * CO5300-flavoured init sequence below. If your glass shows garbage, the
 * usual culprits are (1) the init list, (2) the column/row offset, and
 * (3) RGB vs BGR element order — all adjustable right here / in bsp_pins.h.
 */
#include "bsp_priv.h"
#include "bsp_pins.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_disp";

/*
 * CO5300 init commands. This is a conservative DCS bring-up (sleep-out,
 * 16bpp pixel format, brightness on, tearing-effect line). Vendor demos add
 * a longer tuning block; if you have Waveshare's exact list, paste it here.
 * Format: { cmd, {data bytes...}, data_len, post_delay_ms }.
 */
static const sh8601_lcd_init_cmd_t s_co5300_init[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},   /* select user command set          */
    {0xC4, (uint8_t[]){0x80}, 1, 0},   /* SPI/QSPI write-mode select        */
    {0x3A, (uint8_t[]){0x55}, 1, 0},   /* pixel format: 16bpp / RGB565      */
    {0x35, (uint8_t[]){0x00}, 1, 0},   /* tearing-effect line on            */
    {0x53, (uint8_t[]){0x20}, 1, 1},   /* brightness control block on       */
    {0x51, (uint8_t[]){0xFF}, 1, 0},   /* full brightness from the start    */
    {0x11, (uint8_t[]){0x00}, 0, 80},  /* sleep out                         */
    {0x29, (uint8_t[]){0x00}, 0, 20},  /* display on                        */
};

esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *out_io,
                           esp_lcd_panel_handle_t *out_panel)
{
    ESP_LOGI(TAG, "init QSPI AMOLED %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    const spi_bus_config_t bus_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        BSP_LCD_PCLK_GPIO,
        BSP_LCD_DATA0_GPIO, BSP_LCD_DATA1_GPIO,
        BSP_LCD_DATA2_GPIO, BSP_LCD_DATA3_GPIO,
        BSP_LCD_H_RES * BSP_LCD_V_RES * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg =
        SH8601_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS_GPIO, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
                            (esp_lcd_spi_bus_handle_t)BSP_LCD_QSPI_HOST, &io_cfg, &io),
                        TAG, "panel io");

    const sh8601_vendor_config_t vendor_cfg = {
        .init_cmds      = s_co5300_init,
        .init_cmds_size = sizeof(s_co5300_init) / sizeof(s_co5300_init[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BIT_PER_PIXEL,
        .vendor_config  = (void *)&vendor_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel),
                        TAG, "new panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    if (BSP_LCD_GAP_X || BSP_LCD_GAP_Y) {
        esp_lcd_panel_set_gap(panel, BSP_LCD_GAP_X, BSP_LCD_GAP_Y);
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "disp on");

    *out_io    = io;
    *out_panel = panel;
    return ESP_OK;
}

esp_err_t bsp_display_set_brightness(esp_lcd_panel_io_handle_t io, uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint8_t level = (uint8_t)((percent * 255) / 100);   /* 0x51 write-display-brightness */
    return esp_lcd_panel_io_tx_param(io, 0x51, &level, 1);
}
