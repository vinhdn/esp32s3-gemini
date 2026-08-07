#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Doc % pin qua ADC (BOARD_BATTERY_ADC_CHANNEL) + trang thai sac qua GPIO
// (BOARD_PIN_BATTERY_CHARGE). Bang quy doi ADC -> % duoc hieu chuan rieng
// cho board LC-S3-WiFi-1.54TFT (dien tro phan ap tren mach that).
esp_err_t battery_monitor_init(void);

uint8_t battery_monitor_get_level(void); // 0-100
bool battery_monitor_is_charging(void);

#ifdef __cplusplus
}
#endif
