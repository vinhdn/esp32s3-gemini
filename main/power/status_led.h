#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_PATTERN_OFF,
    LED_PATTERN_SOLID,
    LED_PATTERN_SLOW_BLINK,  // dang ket noi / provisioning
    LED_PATTERN_FAST_BLINK,  // loi
} led_pattern_t;

esp_err_t status_led_init(void);
void status_led_set_pattern(led_pattern_t pattern);

#ifdef __cplusplus
}
#endif
