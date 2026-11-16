/*
 * audio.h — record and playback through the board's two codecs.
 *
 * The board has an ES8311 (speaker, I2C 0x18) and an ES7210 four-channel mic
 * ADC (I2C 0x40) sharing one full-duplex I2S bus. Both are driven through
 * Espressif's esp_codec_dev, which owns their register setup.
 *
 * All the slow work happens on an internal task, so callers just poke a state
 * machine and read back level/progress for drawing. Nothing here blocks.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE  16000
#define AUDIO_CHANNELS     2          /* the ES7210 path is happiest in stereo */
#define AUDIO_MAX_SECONDS  5

typedef enum {
    AUDIO_IDLE = 0,
    AUDIO_RECORDING,
    AUDIO_PLAYING,
    AUDIO_DRONE,        /* granular cloud, runs until stopped */
} audio_state_t;

/* Granular voice parameters. Everything is normalised so the UI can map
 * touch and tilt onto them without knowing about sample rates. */
typedef struct {
    float position;     /* 0..1  where in the take grains are drawn from   */
    float spread;       /* 0..1  randomness around that point              */
    float grain_ms;     /* 15..400  short = buzzy, long = smeared pad      */
    float pitch;        /* 0.25..4  playback rate of each grain            */
    float density;      /* 0..1  how many grains overlap                   */
    float gain;         /* 0..1                                            */
} audio_grain_cfg_t;

/* A live grain, for drawing the cloud that is actually sounding. */
typedef struct {
    float src01;        /* where in the take it is reading   */
    float life01;       /* 0 at birth, 1 as it fades out     */
    float rate;         /* its pitch                         */
    float pan;          /* 0 left .. 1 right                 */
} audio_grain_info_t;

#define AUDIO_MAX_GRAINS 24

/* Brings up I2S and both codecs. Safe to call twice. */
esp_err_t audio_init(void);

/* True once both codecs answered on I2C and opened. If this is false, the
 * mode still runs — it just has nothing to talk to. */
bool audio_ready(void);
bool audio_speaker_ok(void);
bool audio_mic_ok(void);

audio_state_t audio_state(void);

/* A short sine burst straight to the speaker — the simplest proof that the
 * I2S clocking and the ES8311 are alive. */
void audio_play_tone(int freq_hz, int ms);

/* Capture up to AUDIO_MAX_SECONDS into PSRAM, then stop by itself. */
void audio_record(int seconds);

/* Replay whatever was last recorded. */
void audio_play(void);

/* Start/stop the granular drone. It runs continuously off the last take and
 * keeps going until stopped or a new recording begins. */
void audio_drone_start(void);
bool audio_has_take(void);

/* Update the grain parameters. Cheap and safe to call every frame. */
void audio_set_grains(const audio_grain_cfg_t *cfg);

/* Copy out the currently sounding grains, newest state wins. Returns how many
 * were written. Racy by design — it is only used for drawing. */
int audio_grain_snapshot(audio_grain_info_t *out, int max);

void audio_stop(void);

/* 0..1 peak of the most recent input, for a level meter. Updated while
 * recording and also while idle, so the meter is live before you commit. */
float audio_level(void);

/* 0..1 progress through the current record or playback. */
float audio_progress(void);

/* How many frames the last take holds, and a read-only view of it, so a mode
 * can draw the waveform. One frame is AUDIO_CHANNELS samples. */
uint32_t audio_take_frames(void);
const int16_t *audio_take_data(void);

#ifdef __cplusplus
}
#endif
