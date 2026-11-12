/*
 * crystal.c — render the Crystal sprite engine's templates.
 *
 * The engine stores each direction as two 16x24 character maps: `rg` says
 * which material a pixel belongs to (hair, top, skin...) and `px` says which
 * shade of that material's ramp to use. Colouring a character is therefore a
 * table lookup per pixel, which is cheap enough to do straight into the
 * framebuffer every frame — no sprite sheet to bake.
 */
#include "crystal.h"
#include "crystal_data.h"
#include "bsp.h"
#include "fx.h"

/* Which ramp each material letter draws from. */
enum { M_NONE = 0, M_SKIN, M_HAIR, M_TOP, M_BOT, M_BOOT, M_OUTLINE, M_EYE };

static uint8_t material_of(char rg)
{
    switch (rg) {
    case 'h': return M_HAIR;                  /* hair                     */
    case 'e': return M_SKIN;                  /* face                     */
    case 'n': return M_SKIN;                  /* neck                     */
    case 'r': return M_SKIN;                  /* forearm                  */
    case 'k': return M_SKIN;                  /* hand                     */
    case 't': return M_TOP;                   /* torso                    */
    case 'a': return M_TOP;                   /* sleeve                   */
    case 'p': return M_BOT;                   /* hips                     */
    case 'l': return M_BOT;                   /* leg                      */
    case 'f': return M_BOOT;                  /* foot                     */
    case '#': return M_OUTLINE;
    default:  return M_NONE;                  /* '.' transparent          */
    }
}

/* Shade letter -> index into a 3- or 4-entry ramp. */
static int shade_of(char px, int ramp_len)
{
    int i;
    switch (px) {
    case 'D': i = 0; break;                   /* dark  */
    case 'M': i = 1; break;                   /* mid   */
    case 'L': i = 2; break;                   /* light */
    case 'W': i = ramp_len - 1; break;        /* eye white: brightest  */
    case 'P': i = 0; break;                   /* pupil: darkest        */
    default:  i = 1; break;
    }
    return i < ramp_len ? i : ramp_len - 1;
}

static uint16_t pack(uint32_t rgb)
{
    return fx_rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void crystal_make(crystal_char_t *out, uint32_t seed)
{
    /* Mix the seed differently per field so neighbouring seeds do not produce
     * near-identical characters. */
    out->skin = (uint8_t)((seed * 2654435761u >> 13) % CRYS_SKIN_COUNT);
    out->hair = (uint8_t)((seed * 40503u       >> 7)  % CRYS_HAIR_COUNT);
    out->top  = (uint8_t)((seed * 2246822519u  >> 11) % CRYS_CLOTH_COUNT);
    out->bot  = (uint8_t)((seed * 3266489917u  >> 17) % CRYS_CLOTH_COUNT);
}

int crystal_dir_for(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f) return CRYS_S;
    /* Screen space: +y is down, so south is +y. Octant by sign and dominance. */
    float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    bool diag = (ax > 0.414f * ay) && (ay > 0.414f * ax);

    if (diag) {
        if (dy > 0) return dx > 0 ? CRYS_SE : CRYS_SW;
        return dx > 0 ? CRYS_NE : CRYS_NW;
    }
    if (ax > ay) return dx > 0 ? CRYS_E : CRYS_W;
    return dy > 0 ? CRYS_S : CRYS_N;
}

/* The engine stores five directions and mirrors the rest (its DIRSRC table). */
static void source_for(int dir, int *row, bool *mirror)
{
    switch (dir) {
    case CRYS_S:  *row = 0; *mirror = false; break;
    case CRYS_SW: *row = 1; *mirror = false; break;
    case CRYS_W:  *row = 2; *mirror = false; break;
    case CRYS_NW: *row = 3; *mirror = false; break;
    case CRYS_N:  *row = 4; *mirror = false; break;
    case CRYS_NE: *row = 3; *mirror = true;  break;
    case CRYS_E:  *row = 2; *mirror = true;  break;
    default:      *row = 1; *mirror = true;  break;   /* SE */
    }
}

void crystal_draw(uint16_t *fb, int fx, int fy,
                  const crystal_char_t *c, int dir, int bob)
{
    int row;
    bool mirror;
    source_for(dir, &row, &mirror);

    const char *const *rg = CRYS_RG[row];
    const char *const *px = CRYS_PX[row];

    /* Feet sit at the bottom-centre of the template. */
    int x0 = fx - CRYS_W / 2;
    int y0 = fy - CRYS_H - bob;

    for (int ty = 0; ty < CRYS_H; ty++) {
        int sy = y0 + ty;
        if ((unsigned)sy >= BSP_FB_H) continue;
        const char *rrow = rg[ty];
        const char *prow = px[ty];

        for (int tx = 0; tx < CRYS_W; tx++) {
            int src = mirror ? (CRYS_W - 1 - tx) : tx;
            uint8_t mat = material_of(rrow[src]);
            if (mat == M_NONE) continue;

            int sx = x0 + tx;
            if ((unsigned)sx >= BSP_FB_W) continue;

            uint32_t rgb;
            if (mat == M_OUTLINE) {
                rgb = 0x181818;
            } else if (mat == M_BOOT) {
                rgb = 0x2A1E16;
            } else {
                const uint32_t *ramp;
                int len;
                switch (mat) {
                case M_HAIR: ramp = CRYS_HAIR[c->hair]; len = CRYS_HAIR_SHADES; break;
                case M_TOP:  ramp = CRYS_CLOTH[c->top]; len = CRYS_CLOTH_SHADES; break;
                case M_BOT:  ramp = CRYS_CLOTH[c->bot]; len = CRYS_CLOTH_SHADES; break;
                default:     ramp = CRYS_SKIN[c->skin]; len = CRYS_SKIN_SHADES; break;
                }
                char s = prow[src];
                if (s == 'W') rgb = 0xF0F0F8;          /* eye white */
                else if (s == 'P') rgb = 0x181828;     /* pupil     */
                else rgb = ramp[shade_of(s, len)];
            }
            fb[sy * BSP_FB_W + sx] = pack(rgb);
        }
    }
}
