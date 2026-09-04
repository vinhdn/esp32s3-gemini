#include "ble_server.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "board_pins.h"
#include "hud_state.h"

// Parse 3 dinh dang frame giong het esp32/main/ble/waze_hud_ble.c (server
// GATT that dang chay tren board ESP32-S3). Ca 3 deu ghi vao CUNG 1
// characteristic (BOARD_BLE_WRITE_CHAR_UUID), phan biet bang 4 byte magic dau
// + checksum XOR o byte cuoi - KHONG doi thu tu/kich thuoc truong, phai khop
// dung app Android dang gui (VietmapAccessibilityService.kt).

static uint8_t xor_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum ^= data[i];
    return sum;
}

// "VMSL" v? (8 byte, dinh dang cu hon): magic(0-3) version(4) speedLimit(5)
// currentSpeed(6) checksum=XOR(0..6)(7).
static void parse_vmsl(const uint8_t *d, size_t len)
{
    if (len != 8) return;
    if (xor_checksum(d, 7) != d[7]) return;

    hud_state_lock();
    g_hud_state.speed_limit_kmh = d[5];
    g_hud_state.current_speed_kmh = d[6];
    g_hud_state.last_update_ms = millis();
    hud_state_unlock();
}

// "VMSX" v3 (20 byte) - xem doc byte-layout day du trong plan/README.
static void parse_vmsx(const uint8_t *d, size_t len)
{
    if (len != 20) return;
    if (d[4] != 3) return; // version khong ho tro
    if (xor_checksum(d, 19) != d[19]) return;

    uint8_t flags = d[7];
    bool next_limit_valid = (flags & 0x08) != 0;
    bool camera_valid = (flags & 0x10) != 0;
    bool today_weather_valid = (flags & 0x20) != 0;
    bool tomorrow_weather_valid = (flags & 0x40) != 0;

    hud_state_lock();
    g_hud_state.speed_limit_kmh = d[5];
    g_hud_state.current_speed_kmh = d[6];
    g_hud_state.over_speed = (flags & 0x01) != 0;
    g_hud_state.under_min_speed = (flags & 0x02) != 0;
    g_hud_state.min_speed_limit_kmh = d[8];
    g_hud_state.nav_state = d[9];

    g_hud_state.next_limit_valid = next_limit_valid;
    if (next_limit_valid) {
        g_hud_state.next_limit_distance_m = ((uint16_t)d[10] << 8) | d[11];
        g_hud_state.next_limit_kmh = d[12];
    }

    g_hud_state.camera_valid = camera_valid;
    if (camera_valid) {
        g_hud_state.camera_distance_m = ((uint16_t)d[13] << 8) | d[14];
    }

    g_hud_state.today_weather_valid = today_weather_valid;
    if (today_weather_valid) {
        g_hud_state.today_temp_c = (int8_t)d[15];
        g_hud_state.today_condition = d[16];
    }

    g_hud_state.tomorrow_weather_valid = tomorrow_weather_valid;
    if (tomorrow_weather_valid) {
        g_hud_state.tomorrow_temp_c = (int8_t)d[17];
        g_hud_state.tomorrow_condition = d[18];
    }

    g_hud_state.connected = true;
    g_hud_state.last_update_ms = millis();
    hud_state_unlock();

    static uint32_t s_last_log = 0;
    uint32_t now = millis();
    if (now - s_last_log > 2000) {
        s_last_log = now;
        Serial.printf("[VMSX] speed=%u limit=%u next=%u(%um) cam=%um\n",
                      d[6], d[5], d[12], (unsigned)(((uint16_t)d[10] << 8) | d[11]),
                      (unsigned)(((uint16_t)d[13] << 8) | d[14]));
    }
}

// "VHUD" v1 (7 byte): magic(0-3) version(4) flipped(5) checksum=XOR(0..5)(6).
static void parse_vhud(const uint8_t *d, size_t len)
{
    if (len != 7) return;
    if (d[4] != 1) return;
    if (xor_checksum(d, 6) != d[6]) return;

    hud_state_lock();
    g_hud_state.hud_flipped = d[5] != 0;
    hud_state_unlock();
}

// "VWXF" v1 (17 byte, HUD_FORECAST_DAYS=5): magic(0-3) version(4)=1
// dayCount(5)=5 [temp(int8) condition(uint8)]*dayCount(6..15) checksum(16).
// Frame RIENG, khong lien quan VMSX/VietMap - xem WeatherManager.kt
// (buildForecastFrame()/startIndependentBleUpdates(), gui doc lap qua
// ImageRelayBle.sendRawFrame trong BleForegroundService, khong phu thuoc
// VietmapAccessibilityService bat duoc bong bong hay khong).
static void parse_vwxf(const uint8_t *d, size_t len)
{
    if (len < 7) return;
    if (d[4] != 1) return; // version khong ho tro
    uint8_t day_count = d[5];
    if (day_count == 0 || day_count > HUD_FORECAST_DAYS) return;
    size_t expected_len = 6 + (size_t)day_count * 2 + 1;
    if (len != expected_len) return;
    if (xor_checksum(d, expected_len - 1) != d[expected_len - 1]) return;

    hud_state_lock();
    for (uint8_t i = 0; i < day_count; ++i) {
        g_hud_state.forecast_temp_c[i] = (int8_t)d[6 + i * 2];
        g_hud_state.forecast_condition[i] = d[6 + i * 2 + 1];
    }
    g_hud_state.forecast_valid = true;
    g_hud_state.last_update_ms = millis();
    hud_state_unlock();
}

static void handle_frame(const uint8_t *data, size_t len)
{
    if (len < 4) return;
    if (memcmp(data, "VMSX", 4) == 0) {
        parse_vmsx(data, len);
    } else if (memcmp(data, "VMSL", 4) == 0) {
        parse_vmsl(data, len);
    } else if (memcmp(data, "VHUD", 4) == 0) {
        parse_vhud(data, len);
    } else if (memcmp(data, "VWXF", 4) == 0) {
        parse_vwxf(data, len);
    }
    // Magic khac (vd chunk anh JPEG bong bong - hien khong dung o app, xem
    // plan) - bo qua, khong phai loi.
}

// ============================================================================
// Dong JSON "HLP/1" - du lieu dan duong Google Maps, dung CHUNG characteristic
// voi VMSX (ImageRelayBle.sendRawFrame gui thang, khong qua handshake) -
// phan biet bang byte dau '{'/'[', xem waze_hud_ble.c:access_cb ben board S3.
// App gui 1 dong JSON + '\n', co the toi trong 1 write BLE nhung van dem theo
// dong (giong feed_bytes/handle_line ben firmware S3) de an toan neu bi chia
// chunk.
// ============================================================================

#define JSON_LINE_MAX 256
static char s_line_buf[JSON_LINE_MAX];
static size_t s_line_len = 0;
static bool s_line_overflow = false;

static void copy_field(char *dst, size_t dst_size, JsonVariantConst v)
{
    if (v.is<const char *>()) {
        strncpy(dst, v.as<const char *>(), dst_size - 1);
        dst[dst_size - 1] = 0;
    }
}

static void handle_json_line(const char *line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    if (!doc["v"].is<int>() || doc["v"].as<int>() != 1) return;
    if (!doc["t"].is<const char *>()) return;
    const char *type = doc["t"];

    if (strcmp(type, "nav") == 0) {
        hud_state_lock();
        g_hud_state.nav_active = true;
        copy_field(g_hud_state.nav_direction, sizeof(g_hud_state.nav_direction), doc["dir"]);
        copy_field(g_hud_state.nav_distance, sizeof(g_hud_state.nav_distance), doc["dist"]);
        copy_field(g_hud_state.nav_road, sizeof(g_hud_state.nav_road), doc["road"]);
        copy_field(g_hud_state.nav_instruction, sizeof(g_hud_state.nav_instruction), doc["instruction"]);
        copy_field(g_hud_state.nav_time_remaining, sizeof(g_hud_state.nav_time_remaining), doc["time"]);
        copy_field(g_hud_state.nav_total_dist, sizeof(g_hud_state.nav_total_dist), doc["total_dist"]);
        copy_field(g_hud_state.nav_eta, sizeof(g_hud_state.nav_eta), doc["eta"]);
        g_hud_state.last_update_ms = millis();
        hud_state_unlock();
    } else if (strcmp(type, "nav_clear") == 0) {
        hud_state_lock();
        g_hud_state.nav_active = false;
        g_hud_state.nav_direction[0] = 0;
        g_hud_state.nav_distance[0] = 0;
        g_hud_state.nav_road[0] = 0;
        g_hud_state.nav_instruction[0] = 0;
        g_hud_state.nav_time_remaining[0] = 0;
        g_hud_state.nav_total_dist[0] = 0;
        g_hud_state.nav_eta[0] = 0;
        hud_state_unlock();
    }
    // Cac type khac (s/veh/lim/ping - Waze mod/OBD/DatMap/handshake) khong
    // thuoc pham vi VietMap Live + VMSX hien tai, bo qua theo dung "quy tac
    // tuong thich HLP/1" cua firmware S3 (bo qua key/t la khong biet).
}

static void feed_json_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = data[i];
        if (c == '\n') {
            if (!s_line_overflow && s_line_len > 0) {
                s_line_buf[s_line_len] = 0;
                handle_json_line(s_line_buf);
            }
            s_line_len = 0;
            s_line_overflow = false;
        } else if (!s_line_overflow) {
            if (s_line_len >= sizeof(s_line_buf) - 1) {
                s_line_overflow = true;
            } else {
                s_line_buf[s_line_len++] = (char)c;
            }
        }
    }
}

class HudWriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) override
    {
        const std::string &value = pCharacteristic->getValue();
        const uint8_t *data = reinterpret_cast<const uint8_t *>(value.data());
        size_t len = value.size();
        if (len == 0) return;

        if (data[0] == '{' || data[0] == '[') {
            feed_json_bytes(data, len);
        } else {
            handle_frame(data, len);
        }
    }
};

class HudServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer) override
    {
        Serial.println("[BLE] client connected");
        hud_state_lock();
        g_hud_state.connected = true;
        hud_state_unlock();
    }

    void onDisconnect(NimBLEServer *pServer) override
    {
        Serial.println("[BLE] client disconnected, quang ba lai");
        hud_state_lock();
        g_hud_state.connected = false;
        hud_state_unlock();
        // App co the reconnect bat cu luc nao (vd tat/bat man hinh) - phai
        // luon quang ba lai, NimBLE khong tu dong lam viec nay sau disconnect.
        pServer->startAdvertising();
    }
};

void ble_server_init()
{
    NimBLEDevice::init(BOARD_BLE_DEVICE_NAME);
    // Cong suat phat toi da - board CYD (ESP32 classic, anten PCB nho) yeu
    // hon han S3, tang cong suat de giam mat goi/rot ket noi.
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new HudServerCallbacks());

    NimBLEService *service = server->createService(BOARD_BLE_SERVICE_UUID);
    NimBLECharacteristic *writeChar = service->createCharacteristic(
        BOARD_BLE_WRITE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    writeChar->setCallbacks(new HudWriteCallbacks());

    service->start();

    // QUAN TRONG: khong khai bao preferred connection params la nguyen nhan
    // rat pho bien gay Android bao GATT status=133 ngay sau khi connect voi
    // peripheral NimBLE-Arduino (Android xin interval mac dinh khong hop voi
    // ESP32, ESP32 tu choi/timeout) - board S3 (ESP-IDF NimBLE) co the co gia
    // tri mac dinh khac nen khong gap. Dat range an toan thuong dung.
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(BOARD_BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);
    advertising->start();

    Serial.printf("[BLE] dang quang ba ten '%s', service %s\n",
                  BOARD_BLE_DEVICE_NAME, BOARD_BLE_SERVICE_UUID);
}
