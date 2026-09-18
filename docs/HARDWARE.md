# Hardware notes — ESP32-S3-Touch-AMOLED-1.75C

The board (from the product listing):

- **MCU:** ESP32-S3R8, dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, 384 KB ROM
- **Memory:** 8 MB PSRAM (octal), 32 MB external flash
- **Display:** 1.75" AMOLED, **466×466**, 16.7 M colours, capacitive touch
- **Sensors/IO:** 6-axis IMU, dual-mic array, USB-C
- **Power:** AXP2101 PMIC
- **Radio:** Wi-Fi 802.11 b/g/n + BLE (onboard antenna)

## Confirmed silicon and pin map

These values come from Waveshare's own sample code for this exact board variant
([waveshareteam/ESP32-S3-Touch-AMOLED-1.75C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C),
`examples/arduino/libraries/Mylibrary/pin_config.h`), not from guesswork.

| Function | Part | Interface | Driver used |
| --- | --- | --- | --- |
| AMOLED controller | **CO5300** | QSPI | `esp_lcd_sh8601` + CO5300 init list |
| Touch | **CST9217** | I²C `0x5A` | custom (`bsp_touch.c`) |
| IMU | **QMI8658** | I²C `0x6B` | custom (`bsp_imu.c`) |
| PMIC | **AXP2101** | I²C `0x34` | custom (`bsp_power.c`) |

| Signal | GPIO |
| --- | --- |
| `LCD_SCLK` | 38 |
| `LCD_CS` | 12 |
| `LCD_SDIO0..3` | 4, 5, 6, 7 |
| `LCD_RESET` | 1 |
| `IIC_SDA` | 15 |
| `IIC_SCL` | 14 |
| `TP_RST` | 2 |
| `TP_INT` | 11 (unused — see below) |

### Two things worth knowing

**Touch is polled, not interrupt-driven.** The CST9217 pulses its IRQ line
roughly once a second instead of holding it asserted while a finger is down, so
it is useless as a "currently pressed" signal. `bsp_touch.c` polls the
controller from LVGL's input callback and `BSP_TOUCH_INT_GPIO` stays `-1`.

**The CST9217 is not a register-map device.** Reading a touch means: write the
16-bit command `0xD000`, read a 15-byte report, then write `0xD000 + 0xAB` to
acknowledge it. Byte 6 must echo `0xAB` or the frame is stale. Finger 0 occupies
bytes 0..4; byte 5 is the touch count.

## If something still looks wrong

- **Image shifted on the glass** → adjust `BSP_LCD_GAP_X/Y`.
- **Colours inverted or swapped** → toggle `CONFIG_LV_COLOR_16_SWAP` in
  `sdkconfig.defaults`, or the RGB/BGR element order in `bsp_display.c`.
- **Display still dark** → `s_co5300_init` in `bsp_display.c` is a conservative
  generic DCS sequence; Waveshare's vendor driver uses a longer tuning block
  that can be transplanted in.
- **Nothing on I²C** → the serial log prints the QMI8658's WHO_AM_I; a wrong
  value points at bus wiring rather than at any single chip.

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
