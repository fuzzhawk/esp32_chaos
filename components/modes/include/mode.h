/*
 * mode.h — the chaosOS mode contract.
 *
 * There is no launcher and no UI. The system boots into mode 0 and the two
 * physical buttons are the entire navigation:
 *   MODE   (power key) cycles modes
 *   ACTION (BOOT)      does whatever the current mode calls its verb
 */
#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    void (*enter)(void);              /* build/seed state          */
    void (*leave)(void);              /* free state                */
    void (*tick)(uint32_t dt_ms);     /* simulate + draw one frame */
    void (*action)(void);             /* ACTION button             */
} chaos_mode_t;

extern const chaos_mode_t mode_sand;     /* pixel gravity sim  */
extern const chaos_mode_t mode_garden;   /* the garden         */
extern const chaos_mode_t mode_glitch;   /* glitch sigils      */
extern const chaos_mode_t mode_audio;    /* record / playback  */

/* Boots into the first mode and never returns. */
void chaos_run(void);
