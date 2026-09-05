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

// Goi tu ui_refresh() (chay tren lvgl_task, CHINH LA task duy nhat duoc phep
// dung LVGL API - xem main.cpp) MOI TICK de lay bitmap vua giai ma xong (neu
// co) mot cach AN TOAN. QUAN TRONG: truoc day decode_task (mot task RIENG,
// chay tren core khac) goi thang hud_set_warning_icon_image() (dung LVGL
// API: lv_obj_invalidate()/lv_obj_clear_flag() tren canvas) tu ben trong no -
// LVGL KHONG thread-safe, goi tu task khac voi lvgl_task lam hong trang thai
// invalidate/render noi bo cua LVGL, gay dung "chi hien lan dau, khong tu
// update bitmap khac ve sau" (xac nhan qua bao cao that). Ham nay thay the:
// decode_task chi dat co "co bitmap moi" (co mutex bao ve), ui_refresh()
// (lvgl_task) moi thuc su doc + goi hud_set_warning_icon_image().
//
// slot: 0=camera (nua phai anh nguon), 1=next_limit (nua trai) - dung quy
// uoc cu. dest: buffer HUD_WARNING_ICON_SIZE*HUD_WARNING_ICON_SIZE uint16_t
// do CALLER cap phat (vd static trong ui.cpp), duoc dien neu tra ve true.
// Tra ve false (dest KHONG doi) neu chua co bitmap moi ke tu lan lay truoc.
bool icon_stream_take_ready(uint8_t slot, uint16_t *dest);
