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
} audio_state_t;

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
