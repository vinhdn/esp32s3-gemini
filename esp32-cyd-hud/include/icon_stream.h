#pragma once

#include <stddef.h>
#include <stdint.h>

// Nhan + giai ma anh icon canh bao THAT (JPEG ghep 2 icon trai/phai 80x40,
// gui qua dung characteristic BLE voi VMSX/nav - phan biet boi byte dau
// 0xFF, JPEG SOI). Xem VietmapAccessibilityService.kt:composeAlertIcons()/
// sendLowQualityIcon() (phia dien thoai) va esp32/main/display/img_stream.c
// (bo tuong duong ben board ESP32-S3 cu - cung 1 dinh dang wire, chi khac
// backend giai ma vi board nay khong co PSRAM/ROM tjpgd).
//
// Ghep chunk + giai ma dien ra o 2 noi khac nhau: ghep (nhanh, chi copy
// byte) chay ngay trong callback BLE; giai ma (cham hon, dung CPU) chay o 1
// task rieng de khong chan NimBLE host task lau.

void icon_stream_init();

// Goi tu ble_server.cpp onWrite khi dang trong 1 chuoi chunk JPEG (byte dau
// cua CHUNK DAU TIEN la 0xFF/SOI, cac chunk tiep theo KHONG bat dau bang
// 0xFF vi la du lieu JPEG lien tuc bi cat theo MTU - ben goi phai tu giu 1
// co "dang nhan anh" xuyen suot nhieu lan goi, xem access_cb trong
// esp32/main/ble/waze_hud_ble.c ben board S3 cu). AN TOAN goi tu task NimBLE.
//
// Tra ve true neu VAN DANG GIUA 1 frame (chua gap EOI - byte tiep theo van
// nen duoc coi la du lieu anh), false neu frame vua ket thuc/chua bat dau -
// ben goi dung gia tri nay de biet luc nao tat co "dang nhan anh".
bool icon_stream_feed(const uint8_t *data, size_t len);
