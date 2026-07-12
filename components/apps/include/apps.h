/*
 * apps.h — the built-in chaosOS apps.
 *
 * Each app exposes a static descriptor; chaos_apps_register_all() hands them
 * all to the kernel. Add your own by declaring a descriptor and registering it.
 */
#pragma once

#include "chaos_app.h"

extern const chaos_app_desc_t app_life_desc;       /* cellular automata */
extern const chaos_app_desc_t app_tama_desc;       /* tamagotchi        */
extern const chaos_app_desc_t app_companion_desc;  /* weird companion   */

void chaos_apps_register_all(void);
