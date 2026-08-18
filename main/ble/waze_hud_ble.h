#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GATT server (NimBLE) cho Waze HUD Link, giao thuc HLP/1 - xem
// docs/waze-hud-link-sdk-ai-bundle.md (Document 2/4/9). Thay the hoan toan
// GATT server gia lap VIETMAP H50 truoc day (bi ket vi du lieu ghi xuong bi
// ma hoa AES khong ro khoa). HLP/1 la JSON Lines khong ma hoa, co tai lieu
// day du tu Waze mod nen khong can doan mo.
//
// UUID (theo Document 9 - "UUID cua transport"):
//   Service:      8a7e0001-4d6e-4c48-9a9d-484c504c0001
//   TX (write, dien thoai -> board):  8a7e0002-4d6e-4c48-9a9d-484c504c0001
//   RX (notify, board -> dien thoai): 8a7e0003-4d6e-4c48-9a9d-484c504c0001
//   Capabilities (read):              8a7e0004-4d6e-4c48-9a9d-484c504c0001
//   CCCD (0x2902) tren RX duoc NimBLE tu dong tao vi co flag NOTIFY.
//
// Chi giai ma cac field message "s" dang dung cho UI hien tai (spd/lim).
// Cac field khac (trn, dst, st, eta, ...) bi bo qua - mo rong sau neu UI can
// hien thi them.

typedef void (*waze_hud_data_cb_t)(uint16_t speed_kmh, uint16_t limit_kmh, void *ctx);

// Khoi tao NimBLE host + GATT server HLP + bat dau advertise. Goi khi vao
// Car Mode. PHAI dam bao WiFi/esp_wifi da stop truoc do (BLE va WiFi dung
// chung radio, tach rieng de tranh cong don RAM noi bo).
esp_err_t waze_hud_ble_start(waze_hud_data_cb_t cb, void *ctx);

// Dung + giai phong toan bo tai nguyen NimBLE (nimble_port_deinit).
void waze_hud_ble_stop(void);

// true neu dang co dien thoai ket noi BLE.
bool waze_hud_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
