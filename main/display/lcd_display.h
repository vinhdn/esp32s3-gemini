#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao SPI bus + panel ST7789 + LVGL port. Goi 1 lan luc boot truoc khi
// dung bat ky ham nao trong ui_screens.h.
esp_err_t lcd_display_init(void);

// Con tro display LVGL, dung de lvgl_port_lock()/tao man hinh trong ui_screens.c
lv_display_t *lcd_display_get_lvgl_disp(void);

// Chinh do sang backlight (LEDC PWM), 0-100%.
void lcd_display_set_backlight_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif
