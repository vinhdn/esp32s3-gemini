#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao NVS flash (goi mot lan trong app_main truoc khi dung cac ham khac).
esp_err_t nvs_settings_init(void);

// Muc volume loa, tinh theo %, mac dinh 60 neu chua tung luu.
uint8_t nvs_settings_get_volume(void);
esp_err_t nvs_settings_set_volume(uint8_t volume_percent);

// Che do HUD (lat man hinh 180 do de phan chieu len kinh lai hien dung
// chieu) - mac dinh false (khong lat) neu chua tung luu.
bool nvs_settings_get_hud_flip(void);
esp_err_t nvs_settings_set_hud_flip(bool flipped);

#ifdef __cplusplus
}
#endif
