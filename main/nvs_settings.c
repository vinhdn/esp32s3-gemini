#include "nvs_settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_settings";
static const char *NVS_NAMESPACE = "app_cfg";

static const char *KEY_SSID = "wifi_ssid";
static const char *KEY_PASS = "wifi_pass";
static const char *KEY_GEMINI_KEY = "gemini_key";
static const char *KEY_VOLUME = "volume";

#define DEFAULT_VOLUME_PERCENT 60

esp_err_t nvs_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS can duoc xoa va khoi tao lai (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_settings_load(app_settings_t *out)
{
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Chua tung ghi gi, coi nhu chua co cau hinh.
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong mo duoc NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    size_t ssid_len = sizeof(out->wifi_ssid);
    size_t pass_len = sizeof(out->wifi_pass);
    size_t key_len = sizeof(out->gemini_api_key);

    esp_err_t ssid_err = nvs_get_str(handle, KEY_SSID, out->wifi_ssid, &ssid_len);
    esp_err_t pass_err = nvs_get_str(handle, KEY_PASS, out->wifi_pass, &pass_len);
    nvs_get_str(handle, KEY_GEMINI_KEY, out->gemini_api_key, &key_len); // khong bat buoc phai co

    nvs_close(handle);

    out->has_wifi_creds = (ssid_err == ESP_OK && pass_err == ESP_OK && strlen(out->wifi_ssid) > 0);
    return ESP_OK;
}

esp_err_t nvs_settings_save_wifi(const char *ssid, const char *password, const char *gemini_api_key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong mo duoc NVS de ghi: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, KEY_SSID, ssid ? ssid : "");
    if (err == ESP_OK) {
        err = nvs_set_str(handle, KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, KEY_GEMINI_KEY, gemini_api_key ? gemini_api_key : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_settings_clear_wifi(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(handle, KEY_SSID);
    nvs_erase_key(handle, KEY_PASS);
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

uint8_t nvs_settings_get_volume(void)
{
    nvs_handle_t handle;
    uint8_t volume = DEFAULT_VOLUME_PERCENT;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, KEY_VOLUME, &volume);
        nvs_close(handle);
    }
    return volume;
}

esp_err_t nvs_settings_set_volume(uint8_t volume_percent)
{
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, KEY_VOLUME, volume_percent);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
