# Hardware notes — ESP32-S3-Touch-AMOLED-1.75C

The board (from the product listing):

- **MCU:** ESP32-S3R8, dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, 384 KB ROM
- **Memory:** 8 MB PSRAM (octal), 32 MB external flash
- **Display:** 1.75" AMOLED, **466×466**, 16.7 M colours, capacitive touch
- **Sensors/IO:** 6-axis IMU, dual-mic array, USB-C
- **Power:** AXP2101 PMIC
- **Radio:** Wi-Fi 802.11 b/g/n + BLE (onboard antenna)

## Which chips chaosOS assumes

| Function | Assumed part | Driver used | Confidence |
| --- | --- | --- | --- |
| AMOLED controller | **CO5300** (QSPI) | `esp_lcd_sh8601` + CO5300 init list | High for the family; init list is generic |
| Touch | **FT-series** (FT3168) | `esp_lcd_touch_ft5x06` | Medium — some units ship CST9217 |
| IMU | **QMI8658** | custom (`bsp_imu.c`) | High for this board family |
| PMIC | **AXP2101** | custom (`bsp_power.c`) | High |

## ⚠️ You must verify before flashing

None of the GPIO numbers, I²C addresses, or the panel init sequence have been
checked against real hardware. **Open the Waveshare wiki page and schematic for
your exact board revision and reconcile every value in
[`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h).**

Checklist:

- [ ] **I²C pins** (`BSP_I2C_SDA_GPIO`, `BSP_I2C_SCL_GPIO`) — the shared bus for
      touch + IMU + PMIC.
- [ ] **QSPI pins** (`BSP_LCD_PCLK/CS/DATA0..3_GPIO`).
- [ ] **Display reset** — on many of these boards RST is behind a **TCA9554 I²C
      IO expander**, not a GPIO. If so, keep `BSP_LCD_RST_GPIO = -1` and pulse
      reset from `bsp_power.c` after configuring the expander.
- [ ] **Touch controller** — set `BSP_TOUCH_CONTROLLER`, the I²C address, and (if
      wrong) swap the managed driver in `components/bsp/idf_component.yml` +
      `bsp_touch.c`.
- [ ] **Touch INT/RST pins**.
- [ ] **IMU address** — QMI8658 is 0x6A or 0x6B depending on the SA0 strap.
- [ ] **Panel init list** (`s_co5300_init` in `bsp_display.c`) and the
      **column/row gap** (`BSP_LCD_GAP_X/Y`) if the image is shifted.
- [ ] **Byte order** — if colours look swapped, flip `flags.swap_bytes` in
      `bsp.c` (or the RGB/BGR element order in `bsp_display.c`).

## Bring-up tips

1. Get the panel showing *anything* first — a solid fill — before worrying about
   touch or the IMU. `bsp_init()` continues without touch/IMU so you can iterate.
2. Watch the serial log: `bsp_imu` prints WHO_AM_I; a wrong value means the I²C
   address or wiring is off.
3. The AMOLED is bright — `bsp_set_brightness()` writes DCS `0x51`. Start low.

## Not yet wired up

- **Microphones** (no audio input path yet) — the "Oddball" companion could react
  to sound; today it reacts only to motion.
- **Wi-Fi / BLE** — unused; plenty of headroom to add an app that uses them.
- **RTC** — offline decay in Glorp relies on `time()`. With no set RTC the clock
  is boot-relative, so cross-power-cycle aging is best-effort until you add SNTP
  or read the board's RTC (if populated).
