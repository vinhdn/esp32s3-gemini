#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GATT server (NimBLE) cho Car HUD, giao thuc HLP/1.
// UUID (theo Document 9):
//   Service:      8a7e0001-4d6e-4c48-9a9d-484c504c0001
//   TX (write):   8a7e0002-4d6e-4c48-9a9d-484c504c0001
//   RX (notify):  8a7e0003-4d6e-4c48-9a9d-484c504c0001
//   Capabilities: 8a7e0004-4d6e-4c48-9a9d-484c504c0001

// Callback nhan toc do + gioi han (tu Waze/navigation app).
typedef void (*waze_hud_data_cb_t)(uint16_t speed_kmh, uint16_t limit_kmh, void *ctx);

// Du lieu dan duong nhan tu app Android (Google Maps navigation).
typedef struct {
    char direction[24];    // "turn_left", "turn_right", "straight", "u_turn", "arrive", ...
    char distance[16];     // "200m", "1.5km" - khoang cach toi luot re
    char road[64];         // Ten duong se re vao
    char eta[16];          // "10:20 AM"
    char instruction[128]; // Chi dan day du, vd "1.7 kilometers, Keep left"
    char time_remaining[16]; // "36 min" - thoi gian con lai
    char total_dist[16];   // "12 km" - khoang cach tong con lai
} nav_data_t;

typedef void (*waze_hud_nav_cb_t)(const nav_data_t *nav, void *ctx);

// Du lieu xe tu OBD-II (doc boi app Android, truyen qua BLE).
typedef struct {
    int16_t  speed_kmh;       // Toc do xe thuc (OBD PID 0x0D), -1 = khong co
    int16_t  coolant_temp_c;  // Nhiet do nuoc lam mat (PID 0x05), -999 = khong co
    int16_t  intake_temp_c;   // Nhiet do khi nap (PID 0x0F), -999 = khong co
    int16_t  oil_temp_c;      // Nhiet do dau dong co (PID 0x5C), -999 = khong co
    int16_t  rpm;             // Vong tua may (PID 0x0C / 4), -1 = khong co
    // Ap suat lop (TPMS) - 4 banh, don vi kPa. -1 = khong co/khong ho tro.
    int16_t  tire_fl_kpa;     // Truoc trai (Front Left)
    int16_t  tire_fr_kpa;     // Truoc phai (Front Right)
    int16_t  tire_rl_kpa;     // Sau trai (Rear Left)
    int16_t  tire_rr_kpa;     // Sau phai (Rear Right)
} vehicle_data_t;

typedef void (*waze_hud_vehicle_cb_t)(const vehicle_data_t *data, void *ctx);

// Khoi tao NimBLE host + GATT server HLP + bat dau advertise.
esp_err_t waze_hud_ble_start(waze_hud_data_cb_t cb, void *ctx);

// Dat callback nhan thong tin dan duong.
void waze_hud_ble_set_nav_cb(waze_hud_nav_cb_t cb, void *ctx);

// Dat callback nhan thong tin xe (OBD-II).
void waze_hud_ble_set_vehicle_cb(waze_hud_vehicle_cb_t cb, void *ctx);

// Dung + giai phong toan bo tai nguyen NimBLE.
void waze_hud_ble_stop(void);

// true neu dang co dien thoai ket noi BLE.
bool waze_hud_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
