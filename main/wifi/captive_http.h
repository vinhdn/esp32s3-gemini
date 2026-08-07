#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// HTTP server phuc vu trang cau hinh WiFi + Gemini API key (dung trong che do
// SoftAP provisioning). Sau khi nguoi dung submit form thanh cong, thiet bi se
// tu restart de ap dung cau hinh moi.
esp_err_t captive_http_start(void);
esp_err_t captive_http_stop(void);

#ifdef __cplusplus
}
#endif
