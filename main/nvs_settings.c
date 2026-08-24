#include "nvs_settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_settings";
static const char *NVS_NAMESPACE = "app_cfg";
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
