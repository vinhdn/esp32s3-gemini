#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(lv_display_t *disp);

// --- Car Mode (man hinh chinh) ---

// Hien thi man hinh Car Mode (goi 1 lan khi boot).
void ui_show_car_mode(void);

// Cap nhat toc do hien tai va toc do gioi han.
// Bien bao gioi han LUON hien thi to nhat o giua man hinh.
// Toc do hien tai hien nho hon phia duoi.
void ui_car_update(uint16_t speed_kmh, uint16_t limit_kmh);

// Cap nhat thong tin navigation (ten duong, huong di, khoang cach).
// Hien thi o PHIA TREN man hinh, khong de len bien bao.
void ui_nav_update(const char *direction, const char *distance, const char *road, const char *instruction);

// Xoa thong tin navigation.
void ui_nav_clear(void);

// Cap nhat 2 vong tron nho (giong kieu bien bao gioi han, nho hon, o duoi):
// bien bao toc do sap toi (trai) + khoang cach toi camera/canh bao (phai,
// nen vang). limit_kmh<=0 => "!" ; distance_m<=0 => "--" (giong placeholder
// tren bong bong VietMap Live).
void ui_set_next_alert(int16_t next_limit_kmh, int32_t alert_distance_m);

// Cap nhat vi tri hien tai tu Vietmap Live (dong rieng, mau trang).
void ui_set_location(const char *location);

// Nen nhap nhay do khi vuot toc do gioi han.
void ui_flash_over_limit(void);

// Vien bien bao nhap nhay khi gioi han thay doi.
void ui_flash_limit_changed(void);

// Cap nhat trang thai ket noi BLE.
void ui_set_ble_connected(bool connected);

// Cap nhat icon/% pin.
void ui_update_battery(uint8_t percent, bool charging);

// Hien thi % volume overlay tam thoi.
void ui_show_volume_overlay(uint8_t percent);

// Forward-declare vehicle_data_t
#include "waze_hud_ble.h"

// Cap nhat thong tin xe (OBD-II). Hien thi nhiet do/ap suat lop o phan duoi.
void ui_vehicle_update(const vehicle_data_t *data);

#ifdef __cplusplus
}
#endif
