#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GATT server (NimBLE) cho Car HUD, giao thuc HLP/1.
// Ten BLE: VIETMAP_HUD_H50
// UUID Vietmap H50 (Bluetooth base UUID):
//   Service:      0000FFFF-0000-1000-8000-00805F9B34FB
//   TX (read + write with/without response):
//                 00009ABC-0000-1000-8000-00805F9B34FB
//   RX (notify):  00001234-0000-1000-8000-00805F9B34FB
//   Capabilities: 00009ABE-0000-1000-8000-00805F9B34FB

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
    int16_t nav_state;     // navigationState tu VietMap (VMSX), -1 = khong co
    // Bien bao toc do sap toi + camera - 2 khu canh bao DOC LAP tren bong
    // bong (sq_upcoming_alert_left/right, xac nhan qua dump that: trai/phai
    // co khoang cach rieng biet). So tho (khong phai string da format) de UI
    // ve thanh vong tron giong bien bao gioi han. 0 = khong co canh bao
    // (giong quy uoc limit_kmh=0 o cho khac) - cac "nav_data_t nav = {0}"
    // hien co deu tu dong dung sentinel nay.
    int16_t alert_limit_kmh;   // bien bao toc do sap toi (trai)
    int32_t alert_distance_m;  // khoang cach toi bien bao sap toi do
    int32_t camera_distance_m; // khoang cach toi camera (phai) - doc lap
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
