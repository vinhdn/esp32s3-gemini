#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERT_SOUND_SPEED_OVER,     // vuot qua toc do gioi han
    ALERT_SOUND_LIMIT_CHANGED,  // toc do gioi han vua thay doi
} alert_sound_t;

// Tao task rieng phat am thanh canh bao bang tone tong hop qua loa (ES8311),
// khong can file audio. Goi 1 lan sau khi codec_board_init() da chay.
esp_err_t alert_sound_init(void);

// Yeu cau phat 1 am canh bao (non-blocking, xep vao hang doi).
void alert_sound_play(alert_sound_t sound);

#ifdef __cplusplus
}
#endif
