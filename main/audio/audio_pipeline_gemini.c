#include "audio_pipeline_gemini.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "codec_board.h"
#include "gemini_live_client.h"

static const char *TAG = "audio_pipeline";

#define MIC_FRAME_SAMPLES        320                  // 20ms @ 16kHz
#define PLAYBACK_MSGBUF_BYTES    (32 * 1024)           // ~1s du lieu 16kHz mono 16-bit, du de chiu giat mang

// VAD noi bo don gian, CHI de quyet dinh hien thi UI (nghe/dang xu ly), khong
// anh huong toi viec gui audio len Gemini - Gemini Live tu lam VAD phia server
// de quyet dinh turn-taking thuc su.
#define VAD_ENERGY_THRESHOLD    500     // bien do trung binh (PCM16 -32768..32767)
#define VAD_HANGOVER_MS         500     // giu trang thai "dang noi" them bao nhieu ms sau khi im

static MessageBufferHandle_t s_playback_msgbuf = NULL;
static TaskHandle_t s_mic_task_handle = NULL;
static TaskHandle_t s_speaker_task_handle = NULL;
static volatile bool s_talking = false;
static volatile bool s_speaker_busy = false;
static volatile int64_t s_last_voice_us = 0;

// ---- Resample 24kHz -> 16kHz (ty le 3:2), noi suy tuyen tinh, giu trang
// thai giua cac lan goi de khong bi "click" o ranh gioi cac doan audio. ----
typedef struct {
    double phase;
    int16_t prev_sample;
    bool has_prev;
} resampler_state_t;

static resampler_state_t s_resampler = { .phase = 0.0, .prev_sample = 0, .has_prev = false };

static void resampler_reset(void)
{
    s_resampler.phase = 0.0;
    s_resampler.has_prev = false;
}

static size_t resample_24k_to_16k(const int16_t *in, size_t in_count, int16_t *out, size_t out_capacity)
{
    if (in_count == 0) {
        return 0;
    }
    const double step = 24000.0 / 16000.0; // 1.5 mau nguon / mau dich
    int16_t prev = s_resampler.has_prev ? s_resampler.prev_sample : in[0];
    double p = s_resampler.phase;
    size_t out_n = 0;

    while (p < (double)in_count && out_n < out_capacity) {
        size_t idx = (size_t)p;
        double frac = p - (double)idx;
        int16_t s0 = (idx == 0) ? prev : in[idx - 1];
        int16_t s1 = in[idx];
        out[out_n++] = (int16_t)(s0 + frac * (double)(s1 - s0));
        p += step;
    }

    s_resampler.phase = p - (double)in_count;
    s_resampler.prev_sample = in[in_count - 1];
    s_resampler.has_prev = true;
    return out_n;
}

static void mic_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    while (1) {
        if (!s_talking) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int n = codec_board_read(frame, MIC_FRAME_SAMPLES);
        if (n > 0) {
            int64_t sum_abs = 0;
            for (int i = 0; i < n; i++) {
                sum_abs += (frame[i] < 0) ? -frame[i] : frame[i];
            }
            if ((sum_abs / n) > VAD_ENERGY_THRESHOLD) {
                s_last_voice_us = esp_timer_get_time();
            }
            gemini_live_client_send_audio(frame, (size_t)n);
        }
    }
}

static void speaker_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    while (1) {
        size_t got = xMessageBufferReceive(s_playback_msgbuf, frame, sizeof(frame), pdMS_TO_TICKS(100));
        if (got > 0) {
            s_speaker_busy = true;
            codec_board_write(frame, got / sizeof(int16_t));
            s_speaker_busy = false;
        }
    }
}

esp_err_t audio_pipeline_init(void)
{
    s_playback_msgbuf = xMessageBufferCreate(PLAYBACK_MSGBUF_BYTES);
    if (!s_playback_msgbuf) {
        ESP_LOGE(TAG, "Khong tao duoc playback message buffer");
        return ESP_FAIL;
    }

    if (xTaskCreate(mic_task, "mic_task", 4096, NULL, 10, &s_mic_task_handle) != pdPASS) {
        return ESP_FAIL;
    }
    if (xTaskCreate(speaker_task, "speaker_task", 4096, NULL, 10, &s_speaker_task_handle) != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio pipeline da san sang");
    return ESP_OK;
}

void audio_pipeline_start_talking(void)
{
    resampler_reset();
    s_last_voice_us = 0;
    s_talking = true;
}

void audio_pipeline_stop_talking(void)
{
    s_talking = false;
}

void audio_pipeline_on_gemini_audio(const uint8_t *pcm24k_bytes, size_t byte_len)
{
    size_t in_count = byte_len / sizeof(int16_t);
    if (in_count == 0) {
        return;
    }
    const int16_t *in = (const int16_t *)pcm24k_bytes;

    int16_t *out = malloc(in_count * sizeof(int16_t));
    if (!out) {
        ESP_LOGW(TAG, "Het RAM khi resample, bo qua 1 doan audio tra loi");
        return;
    }

    size_t out_n = resample_24k_to_16k(in, in_count, out, in_count);
    if (out_n > 0) {
        size_t sent = xMessageBufferSend(s_playback_msgbuf, out, out_n * sizeof(int16_t), pdMS_TO_TICKS(200));
        if (sent == 0) {
            ESP_LOGW(TAG, "Playback buffer day, mat 1 doan audio tra loi");
        }
    }

    free(out);
}

void audio_pipeline_flush_playback(void)
{
    xMessageBufferReset(s_playback_msgbuf);
}

bool audio_pipeline_is_playback_idle(void)
{
    return xMessageBufferIsEmpty(s_playback_msgbuf) && !s_speaker_busy;
}

bool audio_pipeline_is_user_speaking(void)
{
    if (!s_talking) {
        return false;
    }
    return (esp_timer_get_time() - s_last_voice_us) < (VAD_HANGOVER_MS * 1000);
}
