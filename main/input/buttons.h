#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_EVENT_TALK_CLICK,     // nut talk (BOOT) duoc nhan tha - dung de bat/tat phien noi chuyen
    BUTTON_EVENT_VOL_UP_CLICK,
    BUTTON_EVENT_VOL_UP_LONG,    // giu lau -> tang len 100%
    BUTTON_EVENT_VOL_DOWN_CLICK,
    BUTTON_EVENT_VOL_DOWN_LONG,  // giu lau -> tat tieng (0%)
} button_event_t;

typedef void (*button_event_cb_t)(button_event_t event, void *ctx);

// Tao task polling GPIO cho 3 nut (talk, vol+, vol-) voi debounce + phat hien
// long-press. Goi 1 lan luc boot.
esp_err_t buttons_init(button_event_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
