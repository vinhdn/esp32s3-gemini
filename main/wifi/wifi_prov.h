#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_PROV_STATE_CONNECTING,          // dang thu ket noi STA bang creds da luu
    WIFI_PROV_STATE_CONNECTED,           // da co IP, san sang dung mang
    WIFI_PROV_STATE_PROVISIONING,        // dang o che do AP + captive portal, cho nguoi dung nhap wifi
    WIFI_PROV_STATE_DISCONNECTED,        // mat ket noi STA (se tu retry)
} wifi_prov_state_t;

typedef void (*wifi_prov_state_cb_t)(wifi_prov_state_t state, void *ctx);

// Khoi tao driver WiFi + dang ky callback trang thai. Goi 1 lan trong app_main
// sau khi nvs_settings_init() da chay.
esp_err_t wifi_prov_init(wifi_prov_state_cb_t cb, void *ctx);

// Doc cau hinh trong NVS va tu dong: co creds -> ket noi STA; chua co -> bat
// SoftAP + captive portal de nguoi dung cau hinh. Ham nay khong block.
esp_err_t wifi_prov_start(void);

// Ten SoftAP dang phat khi o che do provisioning (de hien thi len LCD).
// Tra ve chuoi rong neu chua o che do provisioning.
const char *wifi_prov_get_ap_ssid(void);

// true neu dang o che do provisioning vi KET NOI THAT BAI SAU KHI DA CO CREDS
// (het luot retry), khac voi truong hop chua tung cau hinh WiFi lan nao. Dung
// de hien thi thong bao phu hop tren LCD ("mat ket noi" vs "can cau hinh").
bool wifi_prov_is_retry_fallback(void);

// Tam dung WiFi co chu dich (vd chuyen sang Car Mode dung BLE) - tat radio va
// dam bao khong bi hieu nham la "mat ket noi" roi tu dong retry/roi ve
// provisioning. Goi wifi_prov_resume() de bat lai va tiep tuc ket noi binh
// thuong.
void wifi_prov_pause(void);
void wifi_prov_resume(void);

#ifdef __cplusplus
}
#endif
