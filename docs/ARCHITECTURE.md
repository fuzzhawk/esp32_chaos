# chaosOS architecture

chaosOS is deliberately small: a launcher, an app registry with a lifecycle, one
scheduler tick, and a sensor event bus — all riding on FreeRTOS + LVGL.

```
        ┌─────────────────────────────────────────────┐
        │                   apps                       │
        │   app_life    app_tama    app_companion      │
        └───────────────────┬─────────────────────────┘
                            │ chaos_app_desc_t (callbacks)
        ┌───────────────────▼─────────────────────────┐
        │                 chaos_os                     │
        │  launcher · app lifecycle · scheduler tick   │
        │  event bus (FreeRTOS queue) · theme          │
        └───────────────────┬─────────────────────────┘
                            │ lv_obj / bsp_imu_read / events
        ┌───────────────────▼─────────────────────────┐
        │                    bsp                       │
        │  I²C · AMOLED(QSPI) · touch · IMU · power     │
        │  + esp_lvgl_port (LVGL on its own task)       │
        └──────────────────────────────────────────────┘
                     FreeRTOS · ESP-IDF · ESP32-S3
```

## Tasks & threading

There are three relevant execution contexts:

1. **`app_main`** — runs boot (NVS → `bsp_init` → register apps →
   `chaos_os_start`) then returns.
2. **The LVGL port task** (created by `esp_lvgl_port`) — owns all rendering and,
   crucially, runs every `lv_timer`. The scheduler tick is an `lv_timer`, so app
   `on_tick` / `on_event` callbacks execute here, already holding the LVGL lock.
   **Apps may touch LVGL objects freely inside their callbacks.**
3. **`chaos_sensor` task** (pinned to core 1) — reads the IMU at 50 Hz, distils
   samples into `chaos_event_t`s, and `xQueueSend`s them onto the event bus. It
   never touches LVGL.

Anything that must call LVGL from *outside* the LVGL task wraps the work in
`bsp_lvgl_lock()` / `bsp_lvgl_unlock()`.

## The scheduler tick

`scheduler_cb` fires ~30 Hz (`TICK_MS = 33`). Each tick it:

1. computes `dt` since the last tick,
2. drains the event queue, dispatching each event to the **active app's**
   `on_event`,
3. calls the active app's `on_tick(dt)`.

At the launcher (no active app) the tick is a no-op; LVGL handles touch on the
tiles directly.

## App lifecycle

```
launcher tap ─► chaos_os_launch(id)
                  ├─ close current app (on_close + delete root)
                  ├─ create fresh full-screen root container
                  ├─ desc->on_open(app)      // build UI, stash app->user
                  └─ hide launcher, show home button
home button ───► chaos_os_go_home()
                  ├─ desc->on_close(app)     // free app->user
                  ├─ delete root
                  └─ show launcher
```

The kernel owns `app->root`; apps own `app->user`. An app never sees the
launcher or other apps.

## Events

`chaos_event_t` is a small tagged union produced by the sensor task:

| Type | Meaning | Payload |
| --- | --- | --- |
| `CHAOS_EV_SHAKE` | device jerked | `vec.mag` = intensity (g) |
| `CHAOS_EV_TILT`  | orientation (10 Hz) | `vec.x` = roll°, `vec.y` = pitch° |
| `CHAOS_EV_FLIP`  | face-up/down change | `i` = 1 if face-down |
| `CHAOS_EV_BATTERY` | periodic power update | `i` = percent |

How the built-ins use them: Primordial reseeds on `SHAKE`; Oddball moves its gaze
on `TILT`, startles on `SHAKE`, and comments on `FLIP`.

## Persistence

Apps save to NVS (`nvs_flash`), each under its own namespace (`glorp`,
`oddball`). Glorp stores a full state blob plus a wall-clock stamp so it can
reconstruct decay across power cycles; Oddball stores encounter/shake counters.
The 24 MB FAT `storage` partition is reserved for future use (pattern libraries,
logs, larger companion "memories").

## Rendering notes for the round display

The panel is a 466×466 circle, so the theme centres everything and keeps content
out of the clipped corners. Primordial renders its 58×58 grid pixel-exact into a
PSRAM-backed `lv_canvas` (each cell an 8×8 block), masked to a circle so the
simulation lives inside the glass.
