/*
 * chaos_os.h — the chaosOS kernel API.
 *
 * chaosOS is a tiny cooperative app shell on top of FreeRTOS + LVGL: a
 * launcher, an app registry with lifecycle, a scheduler tick, and a sensor
 * event bus. Call bsp_init() first, then chaos_os_start().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "chaos_app.h"
#include "chaos_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the shell (launcher + status bar), register the built-in apps and
 * start the scheduler + sensor tasks. Never returns control of the UI; the
 * caller's task may go idle afterwards. */
void chaos_os_start(void);

/* Register an app. Descriptors must live for the program lifetime (statics). */
void chaos_os_register_app(const chaos_app_desc_t *desc);

/* Open the app with this id (closing whatever is open). false if unknown. */
bool chaos_os_launch(const char *id);

/* Close the current app and return to the launcher. */
void chaos_os_go_home(void);

/* Post an event onto the bus (from any task). Delivered to the active app. */
void chaos_os_post_event(const chaos_event_t *ev);

/* Currently open app, or NULL at the launcher. */
chaos_app_t *chaos_os_active_app(void);

/* Milliseconds since boot (monotonic). */
uint32_t chaos_os_millis(void);

#ifdef __cplusplus
}
#endif
