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

// "Car Mode": so toc do lon giua man hinh + bien bao gioi han toc do (hinh
// tron vien do) o phia tren. An toan bo widget cua che do tro ly giong noi.
void ui_show_car_mode(void);
void ui_car_update(uint16_t speed_kmh, uint16_t limit_kmh);

// Nen man hinh nhap nhay do mo ~2s (goi khi vua vuot toc do gioi han).
void ui_flash_over_limit(void);

// Vien bien bao gioi han nhap nhay vai giay (goi khi gioi han toc do doi).
void ui_flash_limit_changed(void);

// Cap nhat trang thai ket noi BLE toi dien thoai, hien o goc tren trai man
// hinh Car Mode (thay cho status_bar_label dung chung).
void ui_set_ble_connected(bool connected);

#ifdef __cplusplus
}
#endif
