#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "nvs_settings.h"

#include "wifi_prov.h"
#include "lcd_display.h"
#include "ui_screens.h"
#include "codec_board.h"
#include "audio_pipeline_gemini.h"
#include "buttons.h"
#include "battery_monitor.h"
#include "status_led.h"
#include "gemini_live_client.h"

static const char *TAG = "app_main";

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_PROVISIONING,
    APP_STATE_CONNECTING_WIFI,
    APP_STATE_IDLE,
    APP_STATE_ACTIVE, // phien Gemini Live dang mo (dang nghe/dang xu ly/dang tra loi)
} app_state_t;

// Trang thai con trong luc ACTIVE, suy ra tu VAD noi bo (audio_pipeline) +
// hang doi phat, dung de chon icon/text hien thi tren LCD.
typedef enum {
    SUB_NONE,
    SUB_LISTENING,
    SUB_THINKING,
    SUB_SPEAKING,
} active_sub_state_t;

static volatile app_state_t s_app_state = APP_STATE_BOOT;
static active_sub_state_t s_sub_state = SUB_NONE;

// Gop transcript tu Gemini (delta) de hien thi tren LCD.
static char s_user_text_buf[256] = {0};
static char s_ai_text_buf[512] = {0};

static void append_bounded(char *buf, size_t buf_size, const char *chunk)
{
    size_t cur_len = strlen(buf);
    size_t chunk_len = strlen(chunk);
    if (buf_size <= cur_len + 1) {
        return;
    }
    size_t avail = buf_size - cur_len - 1;
    size_t copy_len = (chunk_len < avail) ? chunk_len : avail;
    memcpy(buf + cur_len, chunk, copy_len);
    buf[cur_len + copy_len] = '\0';
}

static void reset_transcripts(void)
{
    s_user_text_buf[0] = '\0';
    s_ai_text_buf[0] = '\0';
    ui_clear_transcripts();
}

static void set_volume(uint8_t percent)
{
    codec_board_set_volume(percent);
    nvs_settings_set_volume(percent);
    ui_show_volume_overlay(percent);
}

static void adjust_volume(int delta)
{
    int v = (int)nvs_settings_get_volume() + delta;
    if (v < 0) {
        v = 0;
    } else if (v > 100) {
        v = 100;
    }
    set_volume((uint8_t)v);
}

static void start_active_session(void)
{
    app_settings_t settings;
    nvs_settings_load(&settings);
    if (strlen(settings.gemini_api_key) == 0) {
        ESP_LOGW(TAG, "Chua co Gemini API key, khong the bat dau phien noi chuyen");
        ui_show_error("Chua co Gemini API key");
        return;
    }

    reset_transcripts();
    s_app_state = APP_STATE_ACTIVE;
    s_sub_state = SUB_NONE;
    ui_show_listening();
    esp_err_t err = gemini_live_client_connect(settings.gemini_api_key);
    if (err != ESP_OK) {
        s_app_state = APP_STATE_IDLE;
        ui_show_error("Khong ket noi duoc Gemini");
    }
}

static void stop_active_session(void)
{
    audio_pipeline_stop_talking();
    gemini_live_client_disconnect();
    s_app_state = APP_STATE_IDLE;
    s_sub_state = SUB_NONE;
    ui_show_idle();
}

// ---- Callback tu wifi_prov ---------------------------------------------------
static void on_wifi_state(wifi_prov_state_t state, void *ctx)
{
    switch (state) {
    case WIFI_PROV_STATE_PROVISIONING:
        s_app_state = APP_STATE_PROVISIONING;
        ui_show_provisioning(wifi_prov_get_ap_ssid(), BOARD_PROV_AP_PASSWORD, wifi_prov_is_retry_fallback());
        status_led_set_pattern(LED_PATTERN_SLOW_BLINK);
        break;
    case WIFI_PROV_STATE_CONNECTING:
        s_app_state = APP_STATE_CONNECTING_WIFI;
        ui_show_connecting();
        status_led_set_pattern(LED_PATTERN_SLOW_BLINK);
        break;
    case WIFI_PROV_STATE_CONNECTED:
        s_app_state = APP_STATE_IDLE;
        ui_show_idle();
        status_led_set_pattern(LED_PATTERN_OFF);
        break;
    case WIFI_PROV_STATE_DISCONNECTED:
        status_led_set_pattern(LED_PATTERN_FAST_BLINK);
        break;
    }
}

// ---- Callback tu gemini_live_client ------------------------------------------
static void on_gemini_audio(const uint8_t *pcm_bytes, size_t byte_len, void *ctx)
{
    audio_pipeline_on_gemini_audio(pcm_bytes, byte_len);
}

static void on_gemini_event(gemini_live_event_t event, void *ctx)
{
    switch (event) {
    case GEMINI_LIVE_EVENT_SESSION_READY:
        audio_pipeline_start_talking();
        s_sub_state = SUB_LISTENING;
        ui_show_listening();
        break;
    case GEMINI_LIVE_EVENT_INTERRUPTED:
        audio_pipeline_flush_playback();
        s_sub_state = SUB_LISTENING;
        ui_show_listening();
        break;
    case GEMINI_LIVE_EVENT_TURN_COMPLETE:
        // AI da tra loi xong 1 luot, session van mo de nghe cau tiep theo cho
        // toi khi nguoi dung bam nut talk de ket thuc.
        break;
    case GEMINI_LIVE_EVENT_DISCONNECTED:
        audio_pipeline_stop_talking();
        if (s_app_state == APP_STATE_ACTIVE) {
            s_app_state = APP_STATE_IDLE;
            s_sub_state = SUB_NONE;
            ui_show_idle();
        }
        break;
    }
}

static void on_gemini_text(gemini_live_text_kind_t kind, const char *text_chunk, void *ctx)
{
    if (!text_chunk) {
        return;
    }
    if (kind == GEMINI_LIVE_TEXT_USER_INPUT) {
        if (s_ai_text_buf[0] != '\0') {
            // AI vua tra loi xong luot truoc, day la cau hoi moi -> lam moi ca 2 dong
            reset_transcripts();
        }
        append_bounded(s_user_text_buf, sizeof(s_user_text_buf), text_chunk);
        ui_set_user_text(s_user_text_buf);
    } else {
        append_bounded(s_ai_text_buf, sizeof(s_ai_text_buf), text_chunk);
        ui_set_ai_text(s_ai_text_buf);
    }
}

// ---- Callback tu buttons ------------------------------------------------------
static void on_button_event(button_event_t event, void *ctx)
{
    switch (event) {
    case BUTTON_EVENT_TALK_CLICK:
        if (s_app_state == APP_STATE_PROVISIONING || s_app_state == APP_STATE_CONNECTING_WIFI) {
            // Loi thoat: bam nut talk khi chua ket noi duoc WiFi se xoa cau
            // hinh da luu va khoi dong lai vao che do provisioning ngay.
            ESP_LOGW(TAG, "Reset cau hinh WiFi theo yeu cau (nut talk luc chua ket noi)");
            nvs_settings_clear_wifi();
            esp_restart();
        } else if (s_app_state == APP_STATE_IDLE) {
            start_active_session();
        } else if (s_app_state == APP_STATE_ACTIVE) {
            stop_active_session();
        }
        break;
    case BUTTON_EVENT_VOL_UP_CLICK:
        adjust_volume(10);
        break;
    case BUTTON_EVENT_VOL_DOWN_CLICK:
        adjust_volume(-10);
        break;
    case BUTTON_EVENT_VOL_UP_LONG:
        set_volume(100);
        break;
    case BUTTON_EVENT_VOL_DOWN_LONG:
        set_volume(0);
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_settings_init());

    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(lcd_display_init());
    ui_init(lcd_display_get_lvgl_disp());

    ESP_ERROR_CHECK(codec_board_init());
    ESP_ERROR_CHECK(audio_pipeline_init());
    ESP_ERROR_CHECK(gemini_live_client_init(on_gemini_audio, on_gemini_event, on_gemini_text, NULL));
    ESP_ERROR_CHECK(battery_monitor_init());
    ESP_ERROR_CHECK(buttons_init(on_button_event, NULL));

    ESP_ERROR_CHECK(wifi_prov_init(on_wifi_state, NULL));
    ESP_ERROR_CHECK(wifi_prov_start());

    ESP_LOGI(TAG, "Khoi dong xong, cho su kien WiFi/nut bam...");

    while (1) {
        ui_update_battery(battery_monitor_get_level(), battery_monitor_is_charging());

        if (s_app_state == APP_STATE_ACTIVE) {
            active_sub_state_t next;
            if (!audio_pipeline_is_playback_idle()) {
                next = SUB_SPEAKING;
            } else if (audio_pipeline_is_user_speaking()) {
                next = SUB_LISTENING;
            } else {
                next = SUB_THINKING;
            }
            if (next != s_sub_state) {
                s_sub_state = next;
                switch (next) {
                case SUB_SPEAKING:
                    ui_show_speaking();
                    break;
                case SUB_LISTENING:
                    ui_show_listening();
                    break;
                case SUB_THINKING:
                    ui_show_thinking();
                    break;
                default:
                    break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
