#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tao cac widget LVGL dung chung cho toan bo app. Goi 1 lan sau khi
// lcd_display_init() da tao xong display LVGL.
void ui_init(lv_display_t *disp);

void ui_show_provisioning(const char *ap_ssid, const char *ap_password, bool wifi_lost);
void ui_show_connecting(void);
void ui_show_idle(void);
void ui_show_listening(void);
void ui_show_thinking(void);   // Gemini dang xu ly, chua co audio tra ve
void ui_show_speaking(void);
void ui_show_error(const char *message);

// Hien thi % volume o giua man hinh trong khoang 1.2s roi tu dong an.
void ui_show_volume_overlay(uint8_t percent);

// Cap nhat icon/% pin o goc man hinh.
void ui_update_battery(uint8_t percent, bool charging);

// Transcript: cau nguoi dung noi (STT) va cau Gemini tra loi, hien thi realtime
// trong luc phien hoi thoai dang mo. Goi lien tuc voi noi dung da gop day du
// (khong phai delta) - ui se tu thay the text cu.
void ui_set_user_text(const char *text);
void ui_set_ai_text(const char *text);
void ui_clear_transcripts(void);

#ifdef __cplusplus
}
#endif
