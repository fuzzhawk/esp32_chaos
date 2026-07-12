# chaosOS

An experimental research OS for the **Waveshare ESP32-S3-Touch-AMOLED-1.75C** —
a 466×466 round AMOLED you can hold in your palm. chaosOS is a tiny app shell on
top of FreeRTOS + LVGL, with three built-in toys:

| App | id | What it is |
| --- | --- | --- |
| **Primordial** | `life` | A cellular-automata sandbox (Conway, HighLife, Seeds, Maze). Draw with a finger, cycle rules, *shake to reseed the universe*. |
| **Glorp** | `tama` | A tamagotchi. Feed / Play / Clean / Sleep. Stats decay in real time — **even while the device is off** — and it can die. Persists to NVS. |
| **Oddball** | `oddball` | A weird companion: one enormous eye that tracks how you tilt the device, blinks, startles when shaken, and mutters cryptic things. It remembers how many times you've met — and how many times you've shaken it. |

> **Status: firmware skeleton, not yet flashed to hardware.** The OS and all
> three apps are fully implemented. What still needs *your* eyes is the
> board-specific hardware config — see the honest caveats below.

## What's real vs. what you must verify

Everything above the hardware line — the launcher, scheduler, event bus, and the
three apps — is complete C you can read and extend. The parts that touch silicon
are written against the standard ESP-IDF drivers for this board's chips
(CO5300 AMOLED over QSPI, QMI8658 IMU, AXP2101 PMIC, an FT-series touch panel),
but **the pin map, I²C addresses, and panel init sequence are board-revision
specific and are not verified against hardware.**

Before you flash, open [`docs/HARDWARE.md`](docs/HARDWARE.md) and reconcile
[`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h) with the
Waveshare wiki + schematic for your unit. Every magic number lives in that one
file on purpose.

## Build & flash

Requires ESP-IDF **v5.2+**.

```bash
idf.py set-target esp32s3
idf.py build           # first build fetches lvgl, esp_lvgl_port, the panel &
                       # touch drivers into managed_components/
idf.py -p /dev/ttyACM0 flash monitor
```

Bump `CONFIG_LOG_DEFAULT_LEVEL` to DEBUG while bringing up the display/touch.

## Layout

```
main/                     app_main: NVS → board → register apps → start kernel
components/
  bsp/                    board support: I²C, AMOLED, touch, IMU, power + LVGL
  chaos_os/               the kernel: launcher, app lifecycle, scheduler, events
  apps/                   app_life, app_tama, app_companion
docs/                     HARDWARE.md, ARCHITECTURE.md
```

## Writing your own app

An app is a static `chaos_app_desc_t` plus four optional callbacks. The kernel
hands you a full-screen container (`app->root`) and destroys it for you:

```c
static void my_open(chaos_app_t *app)  { /* build LVGL UI into app->root */ }
static void my_tick(chaos_app_t *app, uint32_t dt_ms) { /* ~30 Hz */ }
static void my_event(chaos_app_t *app, const chaos_event_t *ev) { /* shakes, tilt… */ }
static void my_close(chaos_app_t *app) { /* free app->user */ }

const chaos_app_desc_t my_app_desc = {
    .id = "mine", .name = "My App", .glyph = "MA", .accent = 0x37E0A6,
    .on_open = my_open, .on_tick = my_tick, .on_event = my_event, .on_close = my_close,
};
```

Register it in `components/apps/apps.c`. Motion events (`CHAOS_EV_SHAKE`,
`CHAOS_EV_TILT`, `CHAOS_EV_FLIP`) and battery updates arrive via `on_event` on
the LVGL task, so handlers may touch the UI directly. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full model.
