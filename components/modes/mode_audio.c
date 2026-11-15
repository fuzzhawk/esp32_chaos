/*
 * mode_audio.c — record and playback test, and the start of the sampler.
 *
 * Deliberately built so the three things that can break are separable without
 * a serial cable:
 *   - two dots at the top say whether each codec answered (left mic, right
 *     speaker). Red means it never opened.
 *   - the centre ring is a live input level meter, so the microphone can be
 *     seen working even if the speaker is silent.
 *   - a tone plays on entry, so the speaker can be heard working even if the
 *     microphone is dead.
 *
 * ACTION records three seconds and plays it straight back. Touch replays the
 * last take.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"
#include "audio.h"

#include "esp_log.h"
#include <math.h>

static const char *TAG = "mode_audio";

#define CX (BSP_FB_W / 2)
#define CY (BSP_FB_H / 2)

static uint32_t s_elapsed;
static bool     s_auto_play;        /* play back as soon as recording ends */
static audio_state_t s_last_state;

/* Dots around a circle, filled up to `frac` — a progress arc without needing
 * an arc rasteriser. */
static void draw_progress(uint16_t *fb, int radius, float frac, uint16_t on, uint16_t off)
{
    const int N = 48;
    for (int i = 0; i < N; i++) {
        float a = (float)i / N * 6.28318f - 1.5708f;
        int x = CX + (int)(cosf(a) * radius);
        int y = CY + (int)(sinf(a) * radius);
        fx_disc(fb, x, y, 3, ((float)i / N <= frac) ? on : off);
    }
}

/* The last take, wrapped around the ring as an oscilloscope. */
static void draw_waveform(uint16_t *fb, int radius, uint16_t col)
{
    uint32_t frames = audio_take_frames();
    if (!frames) return;
    const int16_t *pcm = audio_take_data();

    const int N = 180;
    for (int i = 0; i < N; i++) {
        uint32_t f = (uint32_t)((uint64_t)i * frames / N);
        int16_t v = pcm[f * AUDIO_CHANNELS];          /* left channel */
        float amp = (float)(v < 0 ? -v : v) / 32768.0f;

        float a  = (float)i / N * 6.28318f - 1.5708f;
        int   r0 = radius;
        int   r1 = radius + (int)(amp * 46.0f);
        fx_line(fb,
                CX + (int)(cosf(a) * r0), CY + (int)(sinf(a) * r0),
                CX + (int)(cosf(a) * r1), CY + (int)(sinf(a) * r1), col);
    }
}

static void audio_enter(void)
{
    if (audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "audio bring-up failed");
    }
    s_elapsed = 0;
    s_auto_play = false;
    s_last_state = AUDIO_IDLE;

    /* Two quick notes: if you hear these, the ES8311 and the I2S clock work. */
    audio_play_tone(660, 160);
    ESP_LOGI(TAG, "entered: speaker=%s mic=%s",
             audio_speaker_ok() ? "ok" : "FAILED",
             audio_mic_ok() ? "ok" : "FAILED");
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

    /* Chain record -> playback so one press tests the whole round trip. */
    if (s_auto_play && s_last_state == AUDIO_RECORDING && st == AUDIO_IDLE) {
        s_auto_play = false;
        audio_play();
        st = audio_state();
    }
    s_last_state = st;

    /* Touch replays the last take. */
    uint16_t tx, ty;
    if (bsp_touch_get_point(&tx, &ty) && st == AUDIO_IDLE) audio_play();

    uint16_t bg = fx_rgb(6, 7, 12);
    fx_clear(fb, bg);

    uint16_t accent, dim;
    switch (st) {
    case AUDIO_RECORDING: accent = fx_rgb(255, 70, 90);  dim = fx_rgb(60, 16, 22); break;
    case AUDIO_PLAYING:   accent = fx_rgb(80, 230, 140); dim = fx_rgb(16, 56, 34); break;
    default:              accent = fx_rgb(90, 160, 255); dim = fx_rgb(20, 34, 58); break;
    }

    /* Waveform of the last take sits behind everything. */
    draw_waveform(fb, 150, dim);

    /* Progress around the outside while something is happening. */
    if (st != AUDIO_IDLE) draw_progress(fb, 205, audio_progress(), accent, dim);

    /* Live level meter: a disc that breathes with the input. */
    float lvl = audio_level();
    int r = 40 + (int)(lvl * 95.0f);
    fx_disc(fb, CX, CY, r, dim);
    fx_ring(fb, CX, CY, r, 4, accent);

    /* A slow idle pulse so a silent room still looks alive. */
    if (st == AUDIO_IDLE) {
        int pr = 30 + (int)(6.0f * sinf((float)s_elapsed * 0.003f));
        fx_ring(fb, CX, CY, pr, 2, dim);
    }

    /* Codec health, top-left mic and top-right speaker. */
    uint16_t good = fx_rgb(60, 220, 120), bad = fx_rgb(240, 60, 60);
    fx_disc(fb, CX - 40, 60, 9, audio_mic_ok() ? good : bad);
    fx_disc(fb, CX + 40, 60, 9, audio_speaker_ok() ? good : bad);
}

static void audio_action(void)
{
    if (audio_state() != AUDIO_IDLE) {
        audio_stop();
        return;
    }
    s_auto_play = true;
    audio_record(3);
}

const chaos_mode_t mode_audio = {
    .name = "audio sampler",
    .enter = audio_enter, .leave = audio_leave,
    .tick = audio_tick,   .action = audio_action,
};
