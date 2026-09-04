#pragma once

#include <stdint.h>

#define HUD_FORECAST_DAYS 5

// Trang thai HUD hien tai - mirror dung cac truong cua frame "VMSX" v3 (xem
// ble_server.h/.cpp de biet cach parse). Ghi tu callback BLE (co the chay tren
// task cua NimBLE), doc tu task LVGL - LUON truy cap qua hud_state_lock()/
// hud_state_unlock(), khong doc/ghi truc tiep tu 2 noi khac nhau.
typedef struct {
    bool     connected;           // co client BLE dang ket noi khong

    uint8_t  speed_limit_kmh;     // 0 = khong co bien bao (hien "!" nhu bong bong that)
    uint8_t  current_speed_kmh;
    uint8_t  min_speed_limit_kmh; // 0 = khong ap dung
    bool     over_speed;
    bool     under_min_speed;

    bool     next_limit_valid;
    uint16_t next_limit_distance_m;
    uint8_t  next_limit_kmh;      // co the = 0 (bong bong kieu "sq_" chi co icon)

    bool     camera_valid;
    uint16_t camera_distance_m;

    bool     today_weather_valid;
    int8_t   today_temp_c;
    uint8_t  today_condition;     // 0=nang,1=may,2=mua,3=giong,4=tuyet/suong

    bool     tomorrow_weather_valid;
    int8_t   tomorrow_temp_c;
    uint8_t  tomorrow_condition;

    // Du bao 5 ngay - frame "VWXF" RIENG, gui doc lap khong phu thuoc VietMap
    // (xem ble_server.cpp: parse_vwxf(); Android: WeatherManager.kt
    // buildForecastFrame()/startIndependentBleUpdates()). Khac voi
    // today_/tomorrow_weather_* o tren (van nhung trong VMSX, gan voi
    // VietMap) - 2 duong du lieu nay doc lap, khong ghi de len nhau.
    bool     forecast_valid;
    int8_t   forecast_temp_c[HUD_FORECAST_DAYS];
    uint8_t  forecast_condition[HUD_FORECAST_DAYS];

    uint8_t  nav_state;
    bool     hud_flipped;         // frame VHUD - lat man hinh 180 do

    // Du lieu dan duong Google Maps - nhan qua dong JSON "HLP/1" (cung 1
    // characteristic voi VMSX, phan biet bang byte dau '{'), xem ble_server.cpp.
    // Tat ca la CHUOI THO da dinh dang san boi app Android (giu nguyen don vi/
    // ngon ngu cua Google Maps tren dien thoai) - hien thi truc tiep, khong
    // parse/doi don vi lai o firmware.
    bool     nav_active;          // true tu luc nhan "nav" toi khi "nav_clear"
    char     nav_direction[16];   // "turn_left"/"turn_right"/"straight"/... (xem parse_direction())
    char     nav_distance[24];    // vd "300 m"
    char     nav_road[64];        // vd "Nguyen Trai"
    char     nav_instruction[88]; // vd "Di lan ben phai, sau do di thang 2,1 km"
    char     nav_time_remaining[24]; // vd "12 phut"
    char     nav_total_dist[24];  // vd "8,4 km"
    char     nav_eta[16];         // vd "14:32"

    uint32_t last_update_ms;      // millis() cua lan nhan frame gan nhat
} hud_state_t;

// Khoi tao gia tri mac dinh (goi 1 lan trong setup()).
void hud_state_init();

// Khoa/mo khoa mutex bao ve hud_state - dung xung quanh MOI lan doc/ghi.
void hud_state_lock();
void hud_state_unlock();

// Con tro toi state dung chung - CHI truy cap giua hud_state_lock()/unlock().
extern hud_state_t g_hud_state;
