#pragma once

// img_stream — nhan 1 frame JPEG GHEP 2 icon canh bao (trai=bien bao toc do
// sap toi, phai=camera, warning_alert_image tu
// VietmapAccessibilityService.kt) qua BLE, giai ma bang tjpgd ROM, tach doi
// va ve vao 2 canvas TRON nho gan lam con cua next_limit_circle/
// camera_circle (ui_screens.c) - dung vi tri, dung mau nen/vien san co.

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao module: tao 2 canvas TRON (52x52 RGB565) lam con cua
// left_circle/right_circle, khoi dong task giai ma JPEG. Goi SAU khi
// ui_screens.c da tao 2 vong tron nay va LVGL da san sang.
esp_err_t img_stream_init(lv_obj_t *left_circle, lv_obj_t *right_circle);

// Nhan 1 chunk du lieu anh tu BLE. An toan goi tu BLE ISR/callback context.
// Raw JPEG stream: tu dong nhan biet frame moi qua SOI (0xFF 0xD8) / ket
// thuc qua EOI (0xFF 0xD9), khong can header rieng.
void img_stream_feed_chunk(const uint8_t *data, uint16_t len);

// Tra ve true neu module da khoi tao thanh cong.
bool img_stream_is_ready(void);

#ifdef __cplusplus
}
#endif
