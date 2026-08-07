#include "wifi_prov.h"

#include <string.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "board_config.h"
#include "captive_http.h"
#include "dns_server.h"
#include "nvs_settings.h"

static const char *TAG = "wifi_prov";

// Cho 1 khoang tre giua cac lan reconnect STA thay vi thu lai ngay lap tuc,
// de tong thoi gian cho truoc khi rot ve provisioning la mot khoang "co y
// nghia" (khong qua nhanh gay bat ngo, khong qua lau gay cho doi).
#define WIFI_RECONNECT_DELAY_MS 4000

static wifi_prov_state_cb_t s_state_cb = NULL;
static void *s_state_cb_ctx = NULL;
static char s_ap_ssid[33] = {0};
static int s_retry_count = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
static bool s_provisioning_mode = false;
static bool s_retry_fallback = false;

static void start_provisioning_ap(bool due_to_failure);

static void notify_state(wifi_prov_state_t state)
{
    if (s_state_cb) {
        s_state_cb(state, s_state_cb_ctx);
    }
}

static void reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_provisioning_mode) {
            return; // dang o AP mode, khong con STA de retry
        }
        notify_state(WIFI_PROV_STATE_DISCONNECTED);
        if (s_retry_count < BOARD_WIFI_STA_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Mat ket noi WiFi, thu lai lan %d/%d sau %d ms", s_retry_count,
                     BOARD_WIFI_STA_MAX_RETRY, WIFI_RECONNECT_DELAY_MS);
            if (!s_reconnect_timer) {
                esp_timer_create_args_t timer_args = {
                    .callback = reconnect_timer_cb,
                    .name = "wifi_reconnect",
                };
                esp_timer_create(&timer_args, &s_reconnect_timer);
            }
            esp_timer_start_once(s_reconnect_timer, WIFI_RECONNECT_DELAY_MS * 1000);
        } else {
            ESP_LOGW(TAG, "Ket noi WiFi that bai sau %d lan thu, chuyen sang che do provisioning",
                     BOARD_WIFI_STA_MAX_RETRY);
            start_provisioning_ap(true);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        ESP_LOGI(TAG, "Da ket noi WiFi thanh cong");
        notify_state(WIFI_PROV_STATE_CONNECTED);
    }
}

static void start_provisioning_ap(bool due_to_failure)
{
    s_provisioning_mode = true;
    s_retry_fallback = due_to_failure;

    esp_wifi_stop(); // bo qua loi neu wifi chua tung start (ESP_ERR_WIFI_NOT_STARTED)

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X", BOARD_PROV_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_config = {0};
    size_t ssid_len = strlen(s_ap_ssid);
    memcpy(ap_config.ap.ssid, s_ap_ssid, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    ap_config.ap.channel = BOARD_PROV_AP_CHANNEL;
    ap_config.ap.max_connection = BOARD_PROV_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char *)ap_config.ap.password, BOARD_PROV_AP_PASSWORD, sizeof(ap_config.ap.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_ap_netif, &ip_info);
    dns_server_start(ip_info.ip.addr);
    captive_http_start();

    ESP_LOGI(TAG, "Che do provisioning: ket noi WiFi '%s' (mat khau: %s), mo trinh duyet bat ky trang nao",
             s_ap_ssid, BOARD_PROV_AP_PASSWORD);

    notify_state(WIFI_PROV_STATE_PROVISIONING);
}

static void start_sta(const app_settings_t *settings)
{
    s_provisioning_mode = false;

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, settings->wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, settings->wifi_pass, sizeof(sta_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    notify_state(WIFI_PROV_STATE_CONNECTING);
}

esp_err_t wifi_prov_init(wifi_prov_state_cb_t cb, void *ctx)
{
    s_state_cb = cb;
    s_state_cb_ctx = ctx;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    return ESP_OK;
}

esp_err_t wifi_prov_start(void)
{
    app_settings_t settings;
    ESP_ERROR_CHECK(nvs_settings_load(&settings));

    if (settings.has_wifi_creds) {
        ESP_LOGI(TAG, "Tim thay WiFi da luu, dang ket noi den '%s'", settings.wifi_ssid);
        start_sta(&settings);
    } else {
        ESP_LOGI(TAG, "Chua co cau hinh WiFi, bat che do provisioning");
        start_provisioning_ap(false);
    }
    return ESP_OK;
}

const char *wifi_prov_get_ap_ssid(void)
{
    return s_provisioning_mode ? s_ap_ssid : "";
}

bool wifi_prov_is_retry_fallback(void)
{
    return s_retry_fallback;
}
