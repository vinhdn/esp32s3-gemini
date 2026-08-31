#pragma once

// img_stream — Nhan anh JPEG qua BLE (chunked), giai ma bang tjpgd ROM,
// ve len LVGL canvas RGB565 (240x240). Dung cho hien thi ban do/camera tu
// dien thoai gui xuong.

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao module: tao LVGL canvas (240x240 RGB565) ben trong `parent`,
// khoi dong task giai ma JPEG. Goi SAU khi LVGL da san sang.
esp_err_t img_stream_init(lv_obj_t *parent);

// Nhan 1 chunk du lieu anh tu BLE. An toan goi tu BLE ISR/callback context.
//
// Giao thuc frame:
//   Chunk dau tien: [0xFF, 0xD8, frame_id, total_chunks, ...jpeg_data...]
//   Chunk tiep theo: [frame_id, chunk_index, ...jpeg_data...]
//
// Khi da du cac chunk cua 1 frame, task giai ma se tu dong chay.
void img_stream_feed_chunk(const uint8_t *data, uint16_t len);

// An/hien canvas anh (de chuyen doi giua man hinh HUD va man hinh anh).
void img_stream_show(bool visible);

// Cham nho o goc man hinh bao trang thai ket noi BLE toi dien thoai (xanh =
// connected, do = mat ket noi). Man hinh gio chi con canvas anh bong bong
// nen can 1 dau hieu ket noi toi thieu, khong dua lai toan bo UI toc do cu.
void img_stream_set_connected(bool connected);

// Tra ve true neu module da khoi tao thanh cong.
bool img_stream_is_ready(void);

#ifdef __cplusplus
}
#endif
