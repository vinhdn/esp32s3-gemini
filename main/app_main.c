// Car HUD firmware - Chi che do oto (BLE nhan du lieu tu dien thoai).
// Khong con mode AI/WiFi/voice assistant.

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "nvs_settings.h"

#include "lcd_display.h"
#include "ui_screens.h"
#include "img_stream.h"
#include "codec_board.h"
#include "audio_pipeline.h"
#include "buttons.h"
#include "battery_monitor.h"
#include "status_led.h"
#include "waze_hud_ble.h"
#include "alert_sound.h"

static const char *TAG = "app_main";

// Tam tat AM THANH canh bao (dang co loi, keu khong dung nhu mong doi).
#define CAR_ALERT_SOUND_ENABLED 0

static bool s_car_has_data = false;
static uint16_t s_car_prev_limit = 0;
static bool s_car_was_over_limit = false;
static esp_timer_handle_t s_limit_debounce_timer = NULL;
static int64_t s_limit_last_beep_us = 0;

#define LIMIT_DEBOUNCE_US 400000
#define LIMIT_BEEP_COOLDOWN_US 2000000

// Trang thai ket noi BLE + reset toc do hien thi sau 15s mat ket noi.
static bool s_ble_was_connected = false;
static int64_t s_ble_disconnect_since_us = 0;
static bool s_car_speed_reset_done = false;
#define BLE_DISCONNECT_RESET_US (15LL * 1000000LL)

// ---- Volume control ----
static void set_volume(uint8_t percent)
{
    codec_board_set_volume(percent);
    nvs_settings_set_volume(percent);
    ui_show_volume_overlay(percent);
}

static void adjust_volume(int delta)
{
    int v = (int)nvs_settings_get_volume() + delta;
    if (v < 0) v = 0;
    else if (v > 100) v = 100;
    set_volume((uint8_t)v);
}

// ---- Canh bao toc do ----
static void limit_debounce_cb(void *arg)
{
    s_limit_last_beep_us = esp_timer_get_time();
    ui_flash_limit_changed();
#if CAR_ALERT_SOUND_ENABLED
    alert_sound_play(ALERT_SOUND_LIMIT_CHANGED);
#endif
}

// ---- Callback: nhan du lieu navigation (Waze HUD Link state) ----
static void on_car_data(uint16_t speed_kmh, uint16_t limit_kmh, void *ctx)
{
    ui_car_update(speed_kmh, limit_kmh);

    bool now_over = (limit_kmh > 0 && speed_kmh > limit_kmh);

    if (s_car_has_data) {
        if (limit_kmh != s_car_prev_limit) {
            ESP_LOGI(TAG, "Gioi han toc do doi: %u -> %u km/h", s_car_prev_limit, limit_kmh);
            if (esp_timer_get_time() - s_limit_last_beep_us > LIMIT_BEEP_COOLDOWN_US) {
                if (!s_limit_debounce_timer) {
                    esp_timer_create_args_t targs = {
                        .callback = limit_debounce_cb,
                        .name = "limit_debounce",
                    };
                    esp_timer_create(&targs, &s_limit_debounce_timer);
                }
                esp_timer_stop(s_limit_debounce_timer);
                esp_timer_start_once(s_limit_debounce_timer, LIMIT_DEBOUNCE_US);
            }
        }
        if (now_over && !s_car_was_over_limit) {
            ESP_LOGW(TAG, "Vuot toc do gioi han: %u > %u km/h", speed_kmh, limit_kmh);
            ui_flash_over_limit();
#if CAR_ALERT_SOUND_ENABLED
            alert_sound_play(ALERT_SOUND_SPEED_OVER);
#endif
        }
    }

    s_car_prev_limit = limit_kmh;
    s_car_was_over_limit = now_over;
    s_car_has_data = true;
}

// ---- Callback: nhan thong tin dan duong tu Google Maps ----
static void on_nav_data(const nav_data_t *nav, void *ctx)
{
    if (!nav) return;
    ui_nav_update(nav->direction, nav->distance, nav->road, nav->instruction);

    // navigationState tu VietMap (VMSX) - hien o dong duoi cung.
    if (nav->nav_state >= 0) {
        ui_set_nav_state(nav->nav_state);
    }

    // Cập nhật time remaining / ETA nếu có (hiện trên status bar và bottom)
    if (nav->time_remaining[0] || nav->total_dist[0] || nav->eta[0]) {
        // Sẽ gọi thêm hàm UI riêng - tạm hiển thị qua label có sẵn
        extern void ui_nav_update_meta(const char *time_remaining, const char *total_dist, const char *eta);
        ui_nav_update_meta(nav->time_remaining, nav->total_dist, nav->eta);
    }
}

// ---- Callback: nhan thong tin xe (OBD-II: toc do, nhiet do, ap suat lop) ----
static void on_vehicle_data(const vehicle_data_t *vd, void *ctx)
{
    if (!vd) return;
    ui_vehicle_update(vd);
}

// ---- Callback tu buttons ----
static void on_button_event(button_event_t event, void *ctx)
{
    switch (event) {
    case BUTTON_EVENT_TALK_CLICK:
        // Trong Car Mode, nut Talk khong co chuc nang (da bo voice assistant).
        break;
    case BUTTON_EVENT_TALK_LONG:
        // Khong con mode AI de chuyen - bo qua.
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

static void log_heap_checkpoint(const char *label)
{
    ESP_LOGI(TAG, "[heap] sau %s: free_internal=%u largest_internal_block=%u",
             label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_settings_init());

    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(lcd_display_init());
    ui_init(lcd_display_get_lvgl_disp());
    // Image stream: JPEG decoder + canvas (ẩn mặc định, hiện khi nhận frame)
    img_stream_init(lv_display_get_screen_active(lcd_display_get_lvgl_disp()));
    log_heap_checkpoint("lcd_display+ui_init+img_stream");

    ESP_ERROR_CHECK(codec_board_init());
    log_heap_checkpoint("codec_board_init");

    ESP_ERROR_CHECK(alert_sound_init());

    // Beep 1 tieng luc boot de kiem tra loa hoat dong.
    alert_sound_play(ALERT_SOUND_SPEED_OVER);

    ESP_ERROR_CHECK(audio_pipeline_init());
    log_heap_checkpoint("audio_pipeline_init");

    ESP_ERROR_CHECK(battery_monitor_init());
    ESP_ERROR_CHECK(buttons_init(on_button_event, NULL));

    // Khoi dong thang vao Car Mode (BLE).
    ui_show_car_mode();
    status_led_set_pattern(LED_PATTERN_SOLID);

    esp_err_t err = waze_hud_ble_start(on_car_data, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong bat duoc BLE: %s", esp_err_to_name(err));
    } else {
        waze_hud_ble_set_nav_cb(on_nav_data, NULL);
        waze_hud_ble_set_vehicle_cb(on_vehicle_data, NULL);
    }
    log_heap_checkpoint("ble_start");

    ESP_LOGI(TAG, "Khoi dong xong - Car Mode (BLE). Cho ket noi tu dien thoai...");

    while (1) {
        ui_update_battery(battery_monitor_get_level(), battery_monitor_is_charging());

        bool connected = waze_hud_ble_is_connected();
        if (connected != s_ble_was_connected) {
            ui_set_ble_connected(connected);
            s_ble_was_connected = connected;
        }

        if (connected) {
            s_ble_disconnect_since_us = 0;
            s_car_speed_reset_done = false;
        } else {
            if (s_ble_disconnect_since_us == 0) {
                s_ble_disconnect_since_us = esp_timer_get_time();
            } else if (!s_car_speed_reset_done &&
                       (esp_timer_get_time() - s_ble_disconnect_since_us) > BLE_DISCONNECT_RESET_US) {
                ESP_LOGW(TAG, "Mat ket noi BLE qua 15s, reset toc do hien thi ve 0");
                ui_car_update(0, s_car_prev_limit);
                s_car_speed_reset_done = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
