// Phat am thanh canh bao bang cach tong hop song sin truc tiep (khong can
// file/asset audio nao). Chay trong task rieng de khong lam block UI/BLE.

#include "alert_sound.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board_config.h"
#include "codec_board.h"

static const char *TAG = "alert_sound";

#define TONE_CHUNK_SAMPLES 320 // 20ms @ 16kHz, dong bo voi cau hinh codec

static QueueHandle_t s_queue = NULL;

static void write_tone(double freq_hz, int duration_ms, float volume)
{
    const int sample_rate = BOARD_I2S_SAMPLE_RATE_HZ;
    int remaining = sample_rate * duration_ms / 1000;
    int16_t amplitude = (int16_t)(32000.0f * volume);
    double phase = 0.0;
    double phase_inc = 2.0 * M_PI * freq_hz / sample_rate;
    int16_t buf[TONE_CHUNK_SAMPLES];

    while (remaining > 0) {
        int chunk = remaining < TONE_CHUNK_SAMPLES ? remaining : TONE_CHUNK_SAMPLES;
        for (int i = 0; i < chunk; i++) {
            buf[i] = (int16_t)(amplitude * sin(phase));
            phase += phase_inc;
            if (phase > 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }
        }
        codec_board_write(buf, chunk);
        remaining -= chunk;
    }
}

static void play_speed_over_alert(void)
{
    // 2 tieng "beep" gap nhau - canh bao vuot toc do gioi han.
    write_tone(880.0, 150, 0.6f);
    vTaskDelay(pdMS_TO_TICKS(80));
    write_tone(880.0, 150, 0.6f);
}

static void play_limit_changed_alert(void)
{
    // 1 tieng "beep" that ngan, cao - chi bao hieu nhanh gioi han toc do vua doi.
    write_tone(1300.0, 50, 0.4f);
}

static void alert_task(void *arg)
{
    alert_sound_t sound;
    while (1) {
        if (xQueueReceive(s_queue, &sound, portMAX_DELAY) == pdTRUE) {
            switch (sound) {
            case ALERT_SOUND_SPEED_OVER:
                play_speed_over_alert();
                break;
            case ALERT_SOUND_LIMIT_CHANGED:
                play_limit_changed_alert();
                break;
            }
        }
    }
}

esp_err_t alert_sound_init(void)
{
    s_queue = xQueueCreate(4, sizeof(alert_sound_t));
    if (!s_queue) {
        return ESP_FAIL;
    }
    if (xTaskCreate(alert_task, "alert_sound", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Alert sound task san sang");
    return ESP_OK;
}

void alert_sound_play(alert_sound_t sound)
{
    if (!s_queue) {
        return;
    }
    // Khong cho neu hang doi day - bo qua canh bao thay vi lam nghen task goi.
    xQueueSend(s_queue, &sound, 0);
}
