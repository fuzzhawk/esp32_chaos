/*
 * bsp_pins.h — board pin map for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
 *
 * These values are taken from Waveshare's own sample code for this exact
 * board variant (waveshareteam/ESP32-S3-Touch-AMOLED-1.75C,
 * examples/arduino/libraries/Mylibrary/pin_config.h) rather than guessed.
 *
 * Confirmed silicon on this board:
 *   Display  CO5300   466x466 AMOLED, QSPI
 *   Touch    CST9217  I2C 0x5A (CST92xx family)
 *   IMU      QMI8658  I2C 0x6B
 *   PMIC     AXP2101  I2C 0x34
 */
#pragma once

/* ------------------------------------------------------------------ */
/*  Shared I2C bus: touch + IMU + PMIC (+ RTC) all hang off this bus.  */
/* ------------------------------------------------------------------ */
#define BSP_I2C_PORT            0
#define BSP_I2C_SDA_GPIO        15
#define BSP_I2C_SCL_GPIO        14
/* Waveshare's own examples run this bus at 100kHz. 400kHz needs stronger
 * pull-ups than this board provides and every transaction NAKs. */
#define BSP_I2C_FREQ_HZ         100000

/* ------------------------------------------------------------------ */
/*  AMOLED panel — CO5300 controller over QSPI, 466x466, RGB565.       */
/* ------------------------------------------------------------------ */
#define BSP_LCD_H_RES           466
#define BSP_LCD_V_RES           466
#define BSP_LCD_BIT_PER_PIXEL   16

#define BSP_LCD_QSPI_HOST       1        /* SPI2_HOST */
#define BSP_LCD_PCLK_GPIO       38       /* LCD_SCLK  */
#define BSP_LCD_CS_GPIO         12       /* LCD_CS    */
#define BSP_LCD_DATA0_GPIO      4        /* LCD_SDIO0 */
#define BSP_LCD_DATA1_GPIO      5        /* LCD_SDIO1 */
#define BSP_LCD_DATA2_GPIO      6        /* LCD_SDIO2 */
#define BSP_LCD_DATA3_GPIO      7        /* LCD_SDIO3 */
#define BSP_LCD_RST_GPIO        1        /* LCD_RESET */
#define BSP_LCD_QSPI_FREQ_HZ    (40 * 1000 * 1000)

/* Adjust only if the image sits off-centre on the glass. */
#define BSP_LCD_GAP_X           0
#define BSP_LCD_GAP_Y           0

/* ------------------------------------------------------------------ */
/*  Capacitive touch — CST9217 (CST92xx protocol).                     */
/* ------------------------------------------------------------------ */
#define BSP_TOUCH_I2C_ADDR      0x5A
#define BSP_TOUCH_RST_GPIO      2        /* TP_RST */
/* TP_INT is GPIO11, but per the vendor driver the CST9217 pulses its IRQ
 * about once a second rather than holding it low while touched, so it is
 * useless as a "currently pressed" signal. We poll from the LVGL input
 * callback instead and leave the pin unused. */
#define BSP_TOUCH_INT_GPIO      (-1)

/* ------------------------------------------------------------------ */
/*  6-axis IMU — QMI8658 on the shared I2C bus.                        */
/* ------------------------------------------------------------------ */
#define BSP_IMU_I2C_ADDR        0x6B

/* ------------------------------------------------------------------ */
/*  Power management — AXP2101 PMIC.                                   */
/* ------------------------------------------------------------------ */
#define BSP_AXP2101_I2C_ADDR    0x34
