#pragma once

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

#ifdef __cplusplus
}
#endif
