/*
 * crystal.h — procedural characters, ported from the feed's Crystal sprite
 * engine (www/js/crystal/engine.js).
 *
 * What is ported: the engine's pixel templates and colour ramps. A character
 * is a seed that picks a skin / hair / top / bottom ramp, and drawing is
 * palette substitution over a 16x24 template, with three of the eight
 * directions produced by mirroring.
 *
 * What is NOT ported: the rig. The original computes 21 animations x 8
 * directions from limb angles; that is far more than this device needs or can
 * bake. Walking here is a two-frame bob, which reads correctly at 16px.
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t skin, hair, top, bot;   /* indices into the extracted ramps */
} crystal_char_t;

/* Eight compass directions, matching the engine's DIRS order. */
enum {
    CRYS_S = 0, CRYS_SW, CRYS_W, CRYS_NW, CRYS_N, CRYS_NE, CRYS_E, CRYS_SE
};

/* Deterministic: the same seed always gives the same character. */
void crystal_make(crystal_char_t *out, uint32_t seed);

/* Nearest compass direction for a movement vector (0,0 keeps facing south). */
int crystal_dir_for(float dx, float dy);

/* Draw with the character's FEET at (fx, fy). `bob` lifts the sprite a pixel
 * for the walk cycle; pass 0 when standing. Clipped to the framebuffer. */
void crystal_draw(uint16_t *fb, int fx, int fy,
                  const crystal_char_t *c, int dir, int bob);
