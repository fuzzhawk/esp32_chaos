/* chaos_internal.h — plumbing shared between the kernel and the launcher. */
#pragma once

#include "lvgl.h"
#include "chaos_app.h"

/* Build the launcher UI into `parent`. Tiles launch apps via chaos_os_launch().
 * Returns the launcher root container (kept, shown/hidden by the kernel). */
lv_obj_t *chaos_launcher_build(lv_obj_t *parent,
                               const chaos_app_desc_t *const *apps, int count);
