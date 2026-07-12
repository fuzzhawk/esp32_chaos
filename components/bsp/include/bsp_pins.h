/*
 * bsp_pins.h — board pin map & controller selection for the
 *              Waveshare ESP32-S3-Touch-AMOLED-1.75C.
 *
 * ============================ READ THIS ============================
 *  Every number in this file is BOARD-SPECIFIC. The values below are
 *  the best-known-typical wiring for this family of Waveshare round
 *  AMOLED boards, but they are NOT guaranteed for your exact revision.
 *
 *  Before you flash: open the Waveshare wiki page and the schematic
 *  for "ESP32-S3-Touch-AMOLED-1.75" and confirm each pin, the I2C
 *  addresses, and which controller ICs are populated. Fix anything
 *  that differs HERE and nowhere else — the rest of the firmware reads
 *  only from this file.
 *
 *  Known unknowns on these boards:
 *    - LCD reset / power-enable is sometimes on a TCA9554 I2C IO
 *      expander rather than a direct GPIO. If so, set
 *      BSP_LCD_RST_GPIO to -1 and drive reset from bsp_power.c.
 *    - The touch controller varies (FT3168 / CST9217 / …). Pick one
 *      with BSP_TOUCH_CONTROLLER below.
 * ===================================================================
 */
#pragma once

/* ------------------------------------------------------------------ */
/*  Shared I2C bus: touch + IMU + PMIC (+ RTC) all hang off this bus.  */
/* ------------------------------------------------------------------ */
#define BSP_I2C_PORT            0
#define BSP_I2C_SDA_GPIO        15   /* TODO verify */
#define BSP_I2C_SCL_GPIO        14   /* TODO verify */
#define BSP_I2C_FREQ_HZ         400000

/* ------------------------------------------------------------------ */
/*  AMOLED panel — CO5300 controller over QSPI, 466x466, RGB565.       */
/* ------------------------------------------------------------------ */
#define BSP_LCD_H_RES           466
#define BSP_LCD_V_RES           466
#define BSP_LCD_BIT_PER_PIXEL   16

#define BSP_LCD_QSPI_HOST       1        /* SPI2_HOST */
#define BSP_LCD_PCLK_GPIO       11       /* TODO verify */
#define BSP_LCD_CS_GPIO         12       /* TODO verify */
#define BSP_LCD_DATA0_GPIO      4        /* TODO verify */
#define BSP_LCD_DATA1_GPIO      5        /* TODO verify */
#define BSP_LCD_DATA2_GPIO      6        /* TODO verify */
#define BSP_LCD_DATA3_GPIO      7        /* TODO verify */
#define BSP_LCD_RST_GPIO        (-1)     /* -1 => reset handled elsewhere (expander) */
#define BSP_LCD_TE_GPIO         (-1)     /* tearing-effect line, optional */
#define BSP_LCD_QSPI_FREQ_HZ    (40 * 1000 * 1000)

/* Some panels are mounted with a column/row offset. Adjust if the
 * image is shifted; 466 on a 470-ish glass often needs a small gap. */
#define BSP_LCD_GAP_X           0
#define BSP_LCD_GAP_Y           0

/* ------------------------------------------------------------------ */
/*  Capacitive touch.                                                  */
/* ------------------------------------------------------------------ */
#define BSP_TOUCH_CONTROLLER_FT3168   0
#define BSP_TOUCH_CONTROLLER_CST9217  1
#define BSP_TOUCH_CONTROLLER          BSP_TOUCH_CONTROLLER_FT3168  /* TODO verify */

#define BSP_TOUCH_I2C_ADDR      0x38     /* FT-series default; CST differs */
#define BSP_TOUCH_INT_GPIO      21       /* TODO verify (-1 to poll instead) */
#define BSP_TOUCH_RST_GPIO      (-1)     /* often on the IO expander */

/* ------------------------------------------------------------------ */
/*  6-axis IMU — QMI8658 on the shared I2C bus.                        */
/* ------------------------------------------------------------------ */
#define BSP_IMU_I2C_ADDR        0x6B     /* QMI8658: 0x6B (SA0 high) or 0x6A */

/* ------------------------------------------------------------------ */
/*  Power management — AXP2101 PMIC.                                   */
/* ------------------------------------------------------------------ */
#define BSP_AXP2101_I2C_ADDR    0x34
