/*
 * audio.c — ES8311 playback and ES7210 capture over one full-duplex I2S bus.
 *
 * esp_codec_dev owns the codec registers; this file owns the I2S channels, the
 * take buffer and the worker task. The two codecs share MCLK/BCLK/WS and have
 * their own data lines, so one I2S peripheral in full duplex serves both.
 */
#include "audio.h"
#include "bsp.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio";

#define FRAME_BYTES   (AUDIO_CHANNELS * (int)sizeof(int16_t))
#define MAX_FRAMES    (AUDIO_SAMPLE_RATE * AUDIO_MAX_SECONDS)
#define CHUNK_FRAMES  512                     /* ~32ms per I2S transfer */

static i2s_chan_handle_t      s_tx, s_rx;
static esp_codec_dev_handle_t s_spk, s_mic;
static bool                   s_ready, s_spk_ok, s_mic_ok;

static int16_t  *s_take;                      /* MAX_FRAMES * AUDIO_CHANNELS */
static uint32_t  s_take_frames;               /* what the last take holds */
static volatile audio_state_t s_state;
static volatile uint32_t s_pos, s_target;     /* frames done / wanted */
static volatile float    s_level;
static volatile int      s_tone_hz, s_tone_ms;

/* ------------------------------------------------------------------ */
/*  Bring-up                                                           */
/* ------------------------------------------------------------------ */

static esp_err_t i2s_setup(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;               /* send silence on underrun */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s channel");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK_GPIO,
            .bclk = BSP_I2S_BCLK_GPIO,
            .ws   = BSP_I2S_WS_GPIO,
            .dout = BSP_I2S_DOUT_GPIO,
            .din  = BSP_I2S_DIN_GPIO,
            .invert_flags = { false, false, false },
        },
    };
    /* Both directions run off the same clock, so they get the same config.
     * Deliberately not enabled here — esp_codec_dev enables on open. */
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "i2s rx");
    return ESP_OK;
}

static esp_codec_dev_handle_t make_speaker(const audio_codec_data_if_t *data_if)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = BSP_I2C_PORT,
        .addr       = BSP_ES8311_I2C_ADDR << 1,   /* esp_codec_dev wants 8-bit */
        .bus_handle = bsp_i2c_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl) { ESP_LOGE(TAG, "es8311 i2c ctrl failed"); return NULL; }

    es8311_codec_cfg_t cfg = {
        .ctrl_if    = ctrl,
        .gpio_if    = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin     = BSP_PA_GPIO,
        .use_mclk   = true,
        .hw_gain    = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&cfg);
    if (!codec) { ESP_LOGE(TAG, "es8311 init failed"); return NULL; }

    esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec,
        .data_if  = data_if,
    };
    return esp_codec_dev_new(&dev);
}

static esp_codec_dev_handle_t make_mic(const audio_codec_data_if_t *data_if)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = BSP_I2C_PORT,
        .addr       = BSP_ES7210_I2C_ADDR << 1,
        .bus_handle = bsp_i2c_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl) { ESP_LOGE(TAG, "es7210 i2c ctrl failed"); return NULL; }

    es7210_codec_cfg_t cfg = {
        .ctrl_if      = ctrl,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2,  /* the dual array */
    };
    const audio_codec_if_t *codec = es7210_codec_new(&cfg);
    if (!codec) { ESP_LOGE(TAG, "es7210 init failed"); return NULL; }

    esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec,
        .data_if  = data_if,
    };
    return esp_codec_dev_new(&dev);
}

static void open_devices(void)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = AUDIO_CHANNELS,
        .channel_mask    = 0,
        .sample_rate     = AUDIO_SAMPLE_RATE,
    };
    if (s_spk && esp_codec_dev_open(s_spk, &fs) == ESP_CODEC_DEV_OK) {
        esp_codec_dev_set_out_vol(s_spk, 75);
        s_spk_ok = true;
    } else {
        ESP_LOGW(TAG, "speaker did not open");
    }
    if (s_mic && esp_codec_dev_open(s_mic, &fs) == ESP_CODEC_DEV_OK) {
        esp_codec_dev_set_in_gain(s_mic, 30.0f);
        s_mic_ok = true;
    } else {
        ESP_LOGW(TAG, "microphone did not open");
    }
}

/* ------------------------------------------------------------------ */
/*  Worker                                                             */
/* ------------------------------------------------------------------ */

static void track_level(const int16_t *buf, int frames)
{
    int32_t peak = 0;
    for (int i = 0; i < frames * AUDIO_CHANNELS; i += AUDIO_CHANNELS) {
        int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
        if (v > peak) peak = v;
    }
    float now = (float)peak / 32768.0f;
    /* Fast attack, slow release, so the meter reads like a VU rather than
     * flickering on every chunk. */
    float prev = s_level;
    s_level = (now > prev) ? now : prev * 0.82f + now * 0.18f;
}

static void emit_tone(int freq, int ms)
{
    if (!s_spk_ok) return;
    static int16_t chunk[CHUNK_FRAMES * AUDIO_CHANNELS];

    int total = AUDIO_SAMPLE_RATE * ms / 1000;
    float phase = 0.0f;
    float step  = 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE;

    for (int done = 0; done < total; done += CHUNK_FRAMES) {
        int n = (total - done < CHUNK_FRAMES) ? total - done : CHUNK_FRAMES;
        for (int i = 0; i < n; i++) {
            /* Taper the ends so it is a note, not a click. */
            float env = 1.0f;
            int from_end = total - (done + i);
            if (done + i < 400) env = (done + i) / 400.0f;
            else if (from_end < 400) env = from_end / 400.0f;

            int16_t v = (int16_t)(sinf(phase) * 9000.0f * env);
            phase += step;
            for (int c = 0; c < AUDIO_CHANNELS; c++) chunk[i * AUDIO_CHANNELS + c] = v;
        }
        esp_codec_dev_write(s_spk, chunk, n * FRAME_BYTES);
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t chunk[CHUNK_FRAMES * AUDIO_CHANNELS];

    for (;;) {
        if (s_tone_hz) {
            int hz = s_tone_hz, ms = s_tone_ms;
            s_tone_hz = 0;
            emit_tone(hz, ms);
            continue;
        }

        switch (s_state) {
        case AUDIO_RECORDING: {
            if (!s_mic_ok) { s_state = AUDIO_IDLE; break; }
            uint32_t left = s_target - s_pos;
            int n = left < CHUNK_FRAMES ? (int)left : CHUNK_FRAMES;
            if (n <= 0) {
                s_take_frames = s_pos;
                s_state = AUDIO_IDLE;
                ESP_LOGI(TAG, "recorded %u frames (%.1fs)",
                         (unsigned)s_take_frames,
                         (double)s_take_frames / AUDIO_SAMPLE_RATE);
                break;
            }
            int16_t *dst = &s_take[s_pos * AUDIO_CHANNELS];
            if (esp_codec_dev_read(s_mic, dst, n * FRAME_BYTES) == ESP_CODEC_DEV_OK) {
                track_level(dst, n);
                s_pos += n;
            } else {
                ESP_LOGW(TAG, "read failed, stopping");
                s_take_frames = s_pos;
                s_state = AUDIO_IDLE;
            }
            break;
        }

        case AUDIO_PLAYING: {
            if (!s_spk_ok || s_pos >= s_take_frames) {
                s_state = AUDIO_IDLE;
                break;
            }
            uint32_t left = s_take_frames - s_pos;
            int n = left < CHUNK_FRAMES ? (int)left : CHUNK_FRAMES;
            esp_codec_dev_write(s_spk, &s_take[s_pos * AUDIO_CHANNELS], n * FRAME_BYTES);
            s_pos += n;
            break;
        }

        default:
            /* Idle: keep reading so the level meter stays live. Discarded. */
            if (s_mic_ok &&
                esp_codec_dev_read(s_mic, chunk, CHUNK_FRAMES * FRAME_BYTES) == ESP_CODEC_DEV_OK) {
                track_level(chunk, CHUNK_FRAMES);
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public                                                             */
/* ------------------------------------------------------------------ */

esp_err_t audio_init(void)
{
    if (s_ready) return ESP_OK;

    s_take = heap_caps_malloc((size_t)MAX_FRAMES * FRAME_BYTES, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_take, ESP_ERR_NO_MEM, TAG, "take buffer");

    ESP_RETURN_ON_ERROR(i2s_setup(), TAG, "i2s");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = s_tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data if");

    s_spk = make_speaker(data_if);
    s_mic = make_mic(data_if);
    open_devices();

    s_ready = true;
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio up: speaker=%s mic=%s",
             s_spk_ok ? "ok" : "FAILED", s_mic_ok ? "ok" : "FAILED");
    return ESP_OK;
}

bool audio_ready(void)       { return s_ready; }
bool audio_speaker_ok(void)  { return s_spk_ok; }
bool audio_mic_ok(void)      { return s_mic_ok; }
audio_state_t audio_state(void) { return s_state; }
float audio_level(void)      { return s_level; }
uint32_t audio_take_frames(void)   { return s_take_frames; }
const int16_t *audio_take_data(void) { return s_take; }

float audio_progress(void)
{
    uint32_t total = (s_state == AUDIO_RECORDING) ? s_target : s_take_frames;
    if (!total) return 0.0f;
    float p = (float)s_pos / total;
    return p > 1.0f ? 1.0f : p;
}

void audio_play_tone(int freq_hz, int ms)
{
    s_tone_ms = ms;
    s_tone_hz = freq_hz;          /* set last: the task triggers on this */
}

void audio_record(int seconds)
{
    if (!s_ready || s_state != AUDIO_IDLE) return;
    if (seconds > AUDIO_MAX_SECONDS) seconds = AUDIO_MAX_SECONDS;
    s_pos    = 0;
    s_target = (uint32_t)(AUDIO_SAMPLE_RATE * seconds);
    s_state  = AUDIO_RECORDING;
    ESP_LOGI(TAG, "recording %ds", seconds);
}

void audio_play(void)
{
    if (!s_ready || s_state != AUDIO_IDLE || !s_take_frames) return;
    s_pos   = 0;
    s_state = AUDIO_PLAYING;
    ESP_LOGI(TAG, "playing %u frames", (unsigned)s_take_frames);
}

void audio_stop(void)
{
    if (s_state == AUDIO_RECORDING) s_take_frames = s_pos;
    s_state = AUDIO_IDLE;
}
