#include "captive_http.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "nvs_settings.h"

static const char *TAG = "captive_http";
static httpd_handle_t s_server = NULL;
static esp_timer_handle_t s_restart_timer = NULL;

static const char SETUP_PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Cau hinh thiet bi</title></head>"
    "<body style=\"font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 16px;\">"
    "<h2>Cau hinh WiFi &amp; Gemini</h2>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>Ten WiFi (SSID)</label><br>"
    "<input name=\"ssid\" required maxlength=\"32\" style=\"width:100%;padding:8px;margin:6px 0;box-sizing:border-box;\"><br>"
    "<label>Mat khau WiFi</label><br>"
    "<input name=\"password\" type=\"password\" maxlength=\"64\" style=\"width:100%;padding:8px;margin:6px 0;box-sizing:border-box;\"><br>"
    "<label>Gemini API Key</label><br>"
    "<input name=\"gemini_key\" maxlength=\"128\" style=\"width:100%;padding:8px;margin:6px 0;box-sizing:border-box;\"><br><br>"
    "<button type=\"submit\" style=\"padding:10px 24px;font-size:16px;\">Luu &amp; Khoi dong lai</button>"
    "</form>"
    "</body></html>";

static void url_decode(char *dst, size_t dst_size, const char *src)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; si++) {
        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && isxdigit((unsigned char)src[si + 1]) && isxdigit((unsigned char)src[si + 2])) {
            char hex[3] = { src[si + 1], src[si + 2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

static void restart_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Khoi dong lai de ap dung cau hinh moi...");
    esp_restart();
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Du lieu form khong hop le");
        return ESP_FAIL;
    }

    char body[512];
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    char raw_ssid[96] = {0}, raw_pass[192] = {0}, raw_key[256] = {0};
    httpd_query_key_value(body, "ssid", raw_ssid, sizeof(raw_ssid));
    httpd_query_key_value(body, "password", raw_pass, sizeof(raw_pass));
    httpd_query_key_value(body, "gemini_key", raw_key, sizeof(raw_key));

    char ssid[APP_SETTINGS_SSID_MAX_LEN + 1] = {0};
    char password[APP_SETTINGS_PASS_MAX_LEN + 1] = {0};
    char gemini_key[APP_SETTINGS_GEMINI_KEY_MAX_LEN + 1] = {0};
    url_decode(ssid, sizeof(ssid), raw_ssid);
    url_decode(password, sizeof(password), raw_pass);
    url_decode(gemini_key, sizeof(gemini_key), raw_key);

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID khong duoc de trong");
        return ESP_FAIL;
    }

    esp_err_t err = nvs_settings_save_wifi(ssid, password, gemini_key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Luu NVS that bai: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Khong luu duoc cau hinh");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Da luu cau hinh WiFi SSID='%s', se khoi dong lai", ssid);

    const char *resp =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
        "<body style=\"font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 16px;\">"
        "<h3>Da luu cau hinh. Thiet bi se khoi dong lai va ket noi WiFi...</h3>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    if (s_restart_timer) {
        esp_timer_start_once(s_restart_timer, 1500 * 1000); // 1.5s de HTTP response kip flush
    }
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Bat ky duong dan nao khong khop -> redirect ve trang cau hinh. Day la
    // co che khien dien thoai tu bat popup "captive portal".
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t captive_http_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    if (!s_restart_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = restart_timer_cb,
            .name = "prov_restart",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_restart_timer));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong khoi dong duoc HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_register_uri_handler(s_server, &save_uri);

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    ESP_LOGI(TAG, "Captive portal HTTP server dang chay");
    return ESP_OK;
}

esp_err_t captive_http_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}
