#pragma once

#include <stdbool.h>
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

// Bat/tat che do HUD: MIRROR NGANG (trai-phai, nhu anh guong that - KHONG
// phai xoay 180 do/lat tren-duoi) de dat board tren taplo, phan chieu len
// kinh lai hien dung chieu cho tai xe. Dieu khien MADCTL cua panel truc
// tiep (esp_lcd_panel_mirror), doc lap voi huong panel goc luc init. Co the
// goi bat ky luc nao sau lcd_display_init().
void lcd_display_set_hud_flip(bool flipped);

#ifdef __cplusplus
}
#endif
