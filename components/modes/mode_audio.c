/*
 * mode_audio.c — a granular drone made from whatever you record into it.
 *
 * Record a few seconds, and the device starts droning: dozens of short
 * overlapping grains read out of that recording, each windowed and slightly
 * detuned. Nothing loops in the usual sense — the cloud is resynthesised
 * continuously, so it drifts rather than repeats.
 *
 * Playing it:
 *   touch X   where in the recording the grains are drawn from
 *   touch Y   grain size, from a short buzzing rasp to long smeared pads
 *   tilt L/R  pitch, roughly down an octave to up an octave
 *   tilt F/B  density — how many grains overlap at once
 *   ACTION    record three new seconds; the drone restarts from them
 *
 * With no finger down the scan position drifts on its own, so it keeps
 * evolving if you leave it be.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"
#include "audio.h"

#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "mode_audio";

#define CX (BSP_FB_W / 2)
#define CY (BSP_FB_H / 2)

static audio_grain_cfg_t s_cfg;
static float    s_drift;             /* autonomous scan when untouched */
static uint32_t s_elapsed;
static bool     s_was_recording;

/* The source waveform, drawn once per entry into a small table so the ring
 * does not have to re-scan the whole take every frame. */
#define WAVE_POINTS 180
static uint8_t s_wave[WAVE_POINTS];
static bool    s_wave_ready;

static void build_wave(void)
{
    uint32_t frames = audio_take_frames();
    s_wave_ready = false;
    if (!frames) return;

    const int16_t *pcm = audio_take_data();
    for (int i = 0; i < WAVE_POINTS; i++) {
        uint32_t a = (uint32_t)((uint64_t)i * frames / WAVE_POINTS);
        uint32_t b = (uint32_t)((uint64_t)(i + 1) * frames / WAVE_POINTS);
        if (b <= a) b = a + 1;
        if (b > frames) b = frames;

        int32_t peak = 0;
        for (uint32_t f = a; f < b; f += 8) {       /* every 8th frame is plenty */
            int16_t v = pcm[(size_t)f * AUDIO_CHANNELS];
            int32_t m = v < 0 ? -v : v;
            if (m > peak) peak = m;
        }
        s_wave[i] = (uint8_t)(peak >> 8);           /* 0..127ish */
    }
    s_wave_ready = true;
}

static void audio_enter(void)
{
    if (audio_init() != ESP_OK) ESP_LOGE(TAG, "audio bring-up failed");

    s_cfg.position = 0.0f;
    s_cfg.spread   = 0.10f;
    s_cfg.grain_ms = 120.0f;
    s_cfg.pitch    = 1.0f;
    s_cfg.density  = 0.5f;
    s_cfg.gain     = 0.8f;
    audio_set_grains(&s_cfg);

    s_drift = 0.0f;
    s_elapsed = 0;
    s_was_recording = false;

    if (audio_has_take()) {
        build_wave();
        audio_drone_start();
    } else {
        audio_play_tone(660, 140);    /* nothing to drone on yet */
    }
    ESP_LOGI(TAG, "entered: speaker=%s mic=%s take=%s",
             audio_speaker_ok() ? "ok" : "FAILED",
             audio_mic_ok() ? "ok" : "FAILED",
             audio_has_take() ? "yes" : "none");
}

static void audio_leave(void)
{
    audio_stop();
}

static void audio_tick(uint32_t dt)
{
    s_elapsed += dt;
    uint16_t *fb = bsp_fb();
    audio_state_t st = audio_state();

    /* When a recording finishes, rebuild the waveform and start droning. */
    if (s_was_recording && st != AUDIO_RECORDING) {
        build_wave();
        audio_drone_start();
        st = audio_state();
    }
    s_was_recording = (st == AUDIO_RECORDING);

    /* ---- controls ---- */
    uint16_t tx, ty;
    bool touching = bsp_touch_get_point(&tx, &ty);
    if (touching) {
        s_cfg.position = (float)tx / BSP_FB_W;
        /* Up the screen = fine grains, down = long smeared ones. */
        s_cfg.grain_ms = 18.0f + (float)ty / BSP_FB_H * 340.0f;
    } else {
        /* Drift slowly so an untouched drone still moves. */
        s_drift += (float)dt * 0.000012f;
        if (s_drift > 1.0f) s_drift -= 1.0f;
        s_cfg.position = s_drift;
    }

    float gx, gy;
    bsp_imu_gravity(&gx, &gy);
    /* Roll sets pitch over two octaves, centred on unity when held flat. */
    s_cfg.pitch   = powf(2.0f, gx * 1.0f);
    s_cfg.density = 0.15f + (gy * 0.5f + 0.5f) * 0.85f;
    if (s_cfg.density < 0.05f) s_cfg.density = 0.05f;
    if (s_cfg.density > 1.0f)  s_cfg.density = 1.0f;
    s_cfg.spread  = 0.03f + (1.0f - s_cfg.density) * 0.25f;
    audio_set_grains(&s_cfg);

    /* ---- drawing ---- */
    fx_clear(fb, fx_rgb(5, 5, 9));

    bool recording = (st == AUDIO_RECORDING);
    uint16_t accent = recording ? fx_rgb(255, 70, 90) : fx_rgb(110, 190, 255);
    uint16_t dim    = recording ? fx_rgb(60, 16, 22)  : fx_rgb(18, 38, 60);

    /* Source waveform as a ring: this is the material being granulated. */
    if (s_wave_ready) {
        for (int i = 0; i < WAVE_POINTS; i++) {
            float a = (float)i / WAVE_POINTS * 6.28318f - 1.5708f;
            int r0 = 186;
            int r1 = r0 + (int)(s_wave[i] * 0.42f);
            fx_line(fb, CX + (int)(cosf(a) * r0), CY + (int)(sinf(a) * r0),
                        CX + (int)(cosf(a) * r1), CY + (int)(sinf(a) * r1), dim);
        }
        /* Where the grains are being drawn from right now. */
        float a = s_cfg.position * 6.28318f - 1.5708f;
        fx_line(fb, CX + (int)(cosf(a) * 170), CY + (int)(sinf(a) * 170),
                    CX + (int)(cosf(a) * 215), CY + (int)(sinf(a) * 215), accent);
    }

    /* The cloud: one dot per sounding grain, placed by where it reads from,
     * fading as it ages. Pitch tints it, pan nudges it sideways. */
    audio_grain_info_t g[AUDIO_MAX_GRAINS];
    int n = audio_grain_snapshot(g, AUDIO_MAX_GRAINS);
    for (int i = 0; i < n; i++) {
        float a = g[i].src01 * 6.28318f - 1.5708f;
        /* Grains rise from the centre outward as they live out their window. */
        float r = 40.0f + g[i].life01 * 120.0f;
        int x = CX + (int)(cosf(a) * r) + (int)((g[i].pan - 0.5f) * 26.0f);
        int y = CY + (int)(sinf(a) * r);

        float env = sinf(g[i].life01 * 3.14159f);          /* matches the window */
        int   rad = 2 + (int)(env * 5.0f);
        uint8_t hot = (uint8_t)(90 + 165 * env);
        uint8_t up  = g[i].rate > 1.0f ? 255 : 150;
        fx_disc(fb, x, y, rad, fx_rgb((uint8_t)(hot * up / 255), hot,
                                      (uint8_t)(255 - hot / 2)));
    }

    /* Centre: input level while recording, grain size when droning. */
    if (recording) {
        int r = 30 + (int)(audio_level() * 90.0f);
        fx_ring(fb, CX, CY, r, 5, accent);
        /* progress through the take */
        int arc = (int)(audio_progress() * 48);
        for (int i = 0; i < arc; i++) {
            float a = (float)i / 48 * 6.28318f - 1.5708f;
            fx_disc(fb, CX + (int)(cosf(a) * 228), CY + (int)(sinf(a) * 228), 3, accent);
        }
    } else {
        int r = 22 + (int)(s_cfg.grain_ms * 0.09f);
        fx_ring(fb, CX, CY, r, 3, dim);
        fx_disc(fb, CX, CY, 6, accent);
    }

    /* Codec health, as before: left mic, right speaker. */
    uint16_t good = fx_rgb(60, 220, 120), bad = fx_rgb(240, 60, 60);
    fx_disc(fb, CX - 40, 54, 7, audio_mic_ok() ? good : bad);
    fx_disc(fb, CX + 40, 54, 7, audio_speaker_ok() ? good : bad);
}

static void audio_action(void)
{
    if (audio_state() == AUDIO_RECORDING) return;   /* let it finish */
    s_wave_ready = false;
    audio_record(3);
}

const chaos_mode_t mode_audio = {
    .name = "granular drone",
    .enter = audio_enter, .leave = audio_leave,
    .tick = audio_tick,   .action = audio_action,
};
