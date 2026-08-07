#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_SSID_MAX_LEN      32
#define APP_SETTINGS_PASS_MAX_LEN      64
#define APP_SETTINGS_GEMINI_KEY_MAX_LEN 128

typedef struct {
    bool has_wifi_creds;
    char wifi_ssid[APP_SETTINGS_SSID_MAX_LEN + 1];
    char wifi_pass[APP_SETTINGS_PASS_MAX_LEN + 1];
    char gemini_api_key[APP_SETTINGS_GEMINI_KEY_MAX_LEN + 1];
} app_settings_t;

// Khoi tao NVS flash (goi mot lan trong app_main truoc khi dung cac ham khac).
esp_err_t nvs_settings_init(void);

// Doc toan bo cau hinh da luu. Neu chua tung luu wifi thi has_wifi_creds = false
// va cac chuoi lien quan se rong.
esp_err_t nvs_settings_load(app_settings_t *out);

// Luu SSID/password WiFi + Gemini API key (nhap qua captive portal).
esp_err_t nvs_settings_save_wifi(const char *ssid, const char *password, const char *gemini_api_key);

// Xoa cau hinh WiFi da luu (dung khi ket noi that bai lien tuc, quay lai provisioning).
esp_err_t nvs_settings_clear_wifi(void);

// Muc volume loa, tinh theo %, mac dinh 60 neu chua tung luu.
uint8_t nvs_settings_get_volume(void);
esp_err_t nvs_settings_set_volume(uint8_t volume_percent);

#ifdef __cplusplus
}
#endif
