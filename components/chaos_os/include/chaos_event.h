/*
 * chaos_event.h — the OS event vocabulary.
 *
 * A single sensor task turns raw IMU samples into these high-level events and
 * posts them on the OS event bus. The active app receives them through its
 * on_event() callback, always on the LVGL task, so handlers may touch UI
 * freely.
 */
#pragma once

#include <stdint.h>

typedef enum {
    CHAOS_EV_NONE = 0,
    CHAOS_EV_SHAKE,    /* device shaken; vec.mag = intensity (g) */
    CHAOS_EV_TILT,     /* orientation drift; vec.x=roll, vec.y=pitch (deg) */
    CHAOS_EV_FLIP,     /* screen turned face-down / face-up; i = 1 down, 0 up */
    CHAOS_EV_BATTERY,  /* periodic power update; i = percent */
} chaos_event_type_t;

typedef struct {
    chaos_event_type_t type;
    struct { float x, y, z, mag; } vec;
    int32_t i;
    int64_t ts_ms;
} chaos_event_t;
