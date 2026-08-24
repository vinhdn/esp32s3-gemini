// GATT server (NimBLE) cho Waze HUD Link (giao thuc HLP/1), dua theo
// docs/waze-hud-link-sdk-ai-bundle.md (Document 2/4/9 - code mau ESP32 BLE
// va UUID transport chinh thuc cua Android HUD Link).

#include "waze_hud_ble.h"

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "waze_hud_ble";

#define DEVICE_NAME "VIETMAP_HUD_H1X"
#define HLP_MAX_FRAME 512

// UUID theo dung bang "UUID cua transport" trong Document 9. Byte truyen
// vao BLE_UUID128_INIT theo thu tu little-endian (dao nguoc so voi chuoi
// UUID chuan hien thi vi 8a7e0001-4d6e-4c48-9a9d-484c504c0001).
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
    0x48, 0x4c, 0x6e, 0x4d, 0x01, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
    0x48, 0x4c, 0x6e, 0x4d, 0x02, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
    0x48, 0x4c, 0x6e, 0x4d, 0x03, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t s_caps_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
    0x48, 0x4c, 0x6e, 0x4d, 0x04, 0x00, 0x7e, 0x8a);

typedef struct {
    uint16_t length;
    uint8_t bytes[HLP_MAX_FRAME];
} ble_chunk_t;

static QueueHandle_t s_chunks;
static TaskHandle_t s_protocol_task;

static waze_hud_data_cb_t s_data_cb;
static void *s_cb_ctx;

static waze_hud_nav_cb_t s_nav_cb;
static void *s_nav_cb_ctx;

static waze_hud_vehicle_cb_t s_vehicle_cb;
static void *s_vehicle_cb_ctx;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_rx_handle;
static bool s_notify_enabled;
static volatile bool s_send_dev_pending;
static uint8_t s_own_addr_type;

// Bo dem ghep frame HLP tu cac chunk WRITE cua Android (khong phu thuoc
// ranh gioi packet ATT - 1 dong JSON co the den qua nhieu chunk hoac nhieu
// dong den chung 1 chunk).
static uint8_t s_line_buf[HLP_MAX_FRAME];
static size_t s_line_len;
static bool s_line_overflow;

static int gap_event(struct ble_gap_event *event, void *arg);

static const char *hci_disconnect_reason_str(int reason)
{
    if (reason < BLE_HS_ERR_HCI_BASE) {
        return "khong phai loi HCI (co the la loi ble_hs noi bo)";
    }
    switch (reason - BLE_HS_ERR_HCI_BASE) {
    case 0x08: return "Connection Timeout (mat song/qua xa)";
    case 0x13: return "Remote User Terminated Connection (dien thoai/app chu dong ngat)";
    case 0x14: return "Remote Device Terminated - Low Resources (may ben kia het tai nguyen)";
    case 0x15: return "Remote Device Terminated - Power Off (may ben kia tat Bluetooth/nguon)";
    case 0x16: return "Terminated By Local Host (chinh board tu ngat)";
    case 0x22: return "LMP Response Timeout";
    case 0x3d: return "Connection Failed to be Established";
    case 0x3e: return "Connection Failed - MIC Failure/Sync Timeout";
    default: return "(xem bang HCI Error Codes trong Bluetooth Core Spec)";
    }
}

// Gui 1 dong HLP (JSON + '\n') qua notify RX, chia nho theo MTU - 3 neu can
// (Document 9: "Android yeu cau MTU 247 ... chia moi frame thanh chunk
// khong lon hon MTU - 3"; firmware van phai nhan duoc voi MTU mac dinh 23).
static void send_line(const char *line)
{
    if (!s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    size_t length = strlen(line);
    if (length + 1 > HLP_MAX_FRAME) {
        ESP_LOGW(TAG, "send_line: dong qua dai (%u byte), bo qua", (unsigned)length);
        return;
    }
    char frame[HLP_MAX_FRAME];
    memcpy(frame, line, length);
    frame[length++] = '\n';

    uint16_t mtu = ble_att_mtu(s_conn_handle);
    size_t payload = mtu > 3 ? (size_t)(mtu - 3) : (BLE_ATT_MTU_DFLT - 3);
    for (size_t offset = 0; offset < length; offset += payload) {
        size_t chunk_length = length - offset;
        if (chunk_length > payload) {
            chunk_length = payload;
        }
        int rc = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < 3 && rc == BLE_HS_ENOMEM; ++attempt) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(frame + offset, chunk_length);
            if (!om) {
                rc = BLE_HS_ENOMEM;
            } else {
                rc = ble_gatts_notify_custom(s_conn_handle, s_rx_handle, om);
            }
            if (rc == BLE_HS_ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "Gui notify RX that bai: rc=%d", rc);
            return;
        }
    }
}

// Xu ly 1 message HLP da parse duoc. Theo dung quy tac tuong thich cua
// HLP/1: bo qua key/`t` la khong biet - chi xu ly nhung gi minh can (ping va
// state "s" mang spd/lim cho UI hien tai).
static void handle_line(const char *line, size_t length)
{
    // Log nguyen dong HLP nhan duoc tu dien thoai (JSON dang van ban, khong
    // ma hoa - khac han H50 truoc day) - dung de quan sat toan bo du lieu
    // Waze mod gui xuong khi debug/tich hop.
    ESP_LOGI(TAG, "RX <- dien thoai (%u byte): %s", (unsigned)length, line);

    cJSON *root = cJSON_Parse(line);
    if (!root || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Dong HLP khong phai JSON object hop le, bo qua");
        if (root) {
            cJSON_Delete(root);
        }
        return;
    }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsString(type)) {
        ESP_LOGW(TAG, "Thieu \"v\":1 hoac \"t\" hop le, bo qua theo quy tac tuong thich HLP/1");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "ping") == 0) {
        ESP_LOGI(TAG, "  -> ping, tra loi pong ngay");
        // Phai tra pong ngay, truoc khi lam viec cham khac (Document 9).
        send_line("{\"v\":1,\"t\":\"pong\"}");
    } else if (strcmp(type->valuestring, "s") == 0) {
        cJSON *spd = cJSON_GetObjectItemCaseSensitive(root, "spd");
        cJSON *lim = cJSON_GetObjectItemCaseSensitive(root, "lim");
        cJSON *road = cJSON_GetObjectItemCaseSensitive(root, "road");
        uint16_t speed_kmh = cJSON_IsNumber(spd) ? (uint16_t)spd->valueint : 0;
        uint16_t limit_kmh = cJSON_IsNumber(lim) ? (uint16_t)lim->valueint : 0;
        const char *road_name = (cJSON_IsString(road) && road->valuestring) ? road->valuestring : NULL;
        ESP_LOGI(TAG, "  -> state: spd=%u lim=%u km/h road=%s", speed_kmh, limit_kmh, road_name ? road_name : "(none)");
        if (s_data_cb) {
            s_data_cb(speed_kmh, limit_kmh, s_cb_ctx);
        }
        // Road name từ Vietmap: hiển thị riêng dòng vị trí (không đè lên nav)
        if (road_name && road_name[0]) {
            extern void ui_set_location(const char *location);
            ui_set_location(road_name);
        }
    } else if (strcmp(type->valuestring, "hi") == 0) {
        ESP_LOGI(TAG, "  -> hi (khai bao ben phat, xem lai log RX o tren de biet rate/fields da chap nhan)");
    } else if (strcmp(type->valuestring, "bye") == 0) {
        ESP_LOGI(TAG, "  -> bye (dien thoai chuan bi ngat/dung dan duong)");
    } else if (strcmp(type->valuestring, "nav") == 0) {
        // Thong tin dan duong tu app Android (Google Maps navigation).
        nav_data_t nav = {0};
        cJSON *dir = cJSON_GetObjectItemCaseSensitive(root, "dir");
        cJSON *dist = cJSON_GetObjectItemCaseSensitive(root, "dist");
        cJSON *road = cJSON_GetObjectItemCaseSensitive(root, "road");
        cJSON *eta = cJSON_GetObjectItemCaseSensitive(root, "eta");
        cJSON *instr = cJSON_GetObjectItemCaseSensitive(root, "instruction");
        cJSON *time_j = cJSON_GetObjectItemCaseSensitive(root, "time");
        cJSON *total_dist_j = cJSON_GetObjectItemCaseSensitive(root, "total_dist");

        if (cJSON_IsString(dir) && dir->valuestring) {
            strncpy(nav.direction, dir->valuestring, sizeof(nav.direction) - 1);
        }
        if (cJSON_IsString(dist) && dist->valuestring) {
            strncpy(nav.distance, dist->valuestring, sizeof(nav.distance) - 1);
        }
        if (cJSON_IsString(road) && road->valuestring) {
            strncpy(nav.road, road->valuestring, sizeof(nav.road) - 1);
        }
        if (cJSON_IsString(eta) && eta->valuestring) {
            strncpy(nav.eta, eta->valuestring, sizeof(nav.eta) - 1);
        }
        if (cJSON_IsString(instr) && instr->valuestring) {
            strncpy(nav.instruction, instr->valuestring, sizeof(nav.instruction) - 1);
        }
        if (cJSON_IsString(time_j) && time_j->valuestring) {
            strncpy(nav.time_remaining, time_j->valuestring, sizeof(nav.time_remaining) - 1);
        }
        if (cJSON_IsString(total_dist_j) && total_dist_j->valuestring) {
            strncpy(nav.total_dist, total_dist_j->valuestring, sizeof(nav.total_dist) - 1);
        }

        ESP_LOGI(TAG, "  -> nav: dir=%s dist=%s road=%s eta=%s", nav.direction, nav.distance, nav.road, nav.eta);
        if (s_nav_cb) {
            s_nav_cb(&nav, s_nav_cb_ctx);
        }
    } else if (strcmp(type->valuestring, "veh") == 0) {
        // Thong tin xe tu OBD-II (doc boi app Android).
        vehicle_data_t vd = {
            .speed_kmh = -1, .coolant_temp_c = -999, .intake_temp_c = -999,
            .oil_temp_c = -999, .rpm = -1,
            .tire_fl_kpa = -1, .tire_fr_kpa = -1, .tire_rl_kpa = -1, .tire_rr_kpa = -1,
        };
        cJSON *spd = cJSON_GetObjectItemCaseSensitive(root, "spd");
        cJSON *coolant = cJSON_GetObjectItemCaseSensitive(root, "coolant");
        cJSON *intake = cJSON_GetObjectItemCaseSensitive(root, "intake");
        cJSON *oil = cJSON_GetObjectItemCaseSensitive(root, "oil");
        cJSON *rpm_j = cJSON_GetObjectItemCaseSensitive(root, "rpm");
        cJSON *tires = cJSON_GetObjectItemCaseSensitive(root, "tires");

        if (cJSON_IsNumber(spd)) vd.speed_kmh = (int16_t)spd->valueint;
        if (cJSON_IsNumber(coolant)) vd.coolant_temp_c = (int16_t)coolant->valueint;
        if (cJSON_IsNumber(intake)) vd.intake_temp_c = (int16_t)intake->valueint;
        if (cJSON_IsNumber(oil)) vd.oil_temp_c = (int16_t)oil->valueint;
        if (cJSON_IsNumber(rpm_j)) vd.rpm = (int16_t)rpm_j->valueint;

        if (cJSON_IsObject(tires)) {
            cJSON *fl = cJSON_GetObjectItemCaseSensitive(tires, "fl");
            cJSON *fr = cJSON_GetObjectItemCaseSensitive(tires, "fr");
            cJSON *rl = cJSON_GetObjectItemCaseSensitive(tires, "rl");
            cJSON *rr = cJSON_GetObjectItemCaseSensitive(tires, "rr");
            if (cJSON_IsNumber(fl)) vd.tire_fl_kpa = (int16_t)fl->valueint;
            if (cJSON_IsNumber(fr)) vd.tire_fr_kpa = (int16_t)fr->valueint;
            if (cJSON_IsNumber(rl)) vd.tire_rl_kpa = (int16_t)rl->valueint;
            if (cJSON_IsNumber(rr)) vd.tire_rr_kpa = (int16_t)rr->valueint;
        }

        ESP_LOGI(TAG, "  -> veh: spd=%d rpm=%d coolant=%d oil=%d tires=[%d,%d,%d,%d]kPa",
                 vd.speed_kmh, vd.rpm, vd.coolant_temp_c, vd.oil_temp_c,
                 vd.tire_fl_kpa, vd.tire_fr_kpa, vd.tire_rl_kpa, vd.tire_rr_kpa);
        if (s_vehicle_cb) {
            s_vehicle_cb(&vd, s_vehicle_cb_ctx);
        }
    } else if (strcmp(type->valuestring, "lim") == 0) {
        // Thong tin toc do gioi han tu DatMap (canh bao giao thong Viet Nam).
        cJSON *limit_j = cJSON_GetObjectItemCaseSensitive(root, "limit");
        cJSON *cam_dist_j = cJSON_GetObjectItemCaseSensitive(root, "cam_dist");
        cJSON *cam_type_j = cJSON_GetObjectItemCaseSensitive(root, "cam_type");

        uint16_t limit = cJSON_IsNumber(limit_j) ? (uint16_t)limit_j->valueint : 0;
        int cam_dist = cJSON_IsNumber(cam_dist_j) ? cam_dist_j->valueint : -1;
        const char *cam_type = (cJSON_IsString(cam_type_j) && cam_type_j->valuestring) ? cam_type_j->valuestring : "";

        ESP_LOGI(TAG, "  -> lim (DatMap): limit=%u cam_dist=%d cam_type=%s", limit, cam_dist, cam_type);

        // Cap nhat gioi han toc do hien thi tren bien bao
        if (limit > 0 && s_data_cb) {
            // Gui lai speed hien tai (giu nguyen) voi limit moi tu DatMap
            s_data_cb(0, limit, s_cb_ctx);
        }
    } else {
        ESP_LOGI(TAG, "  -> t=\"%s\" chua duoc xu ly, bo qua theo quy tac tuong thich HLP/1", type->valuestring);
    }

    cJSON_Delete(root);
}

static void feed_bytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (c == '\n') {
            if (!s_line_overflow) {
                size_t n = s_line_len;
                if (n && s_line_buf[n - 1] == '\r') {
                    --n;
                }
                s_line_buf[n] = 0;
                handle_line((const char *)s_line_buf, n);
            }
            s_line_len = 0;
            s_line_overflow = false;
        } else if (!s_line_overflow) {
            if (s_line_len >= sizeof(s_line_buf) - 1) {
                s_line_overflow = true;
                ESP_LOGW(TAG, "Dong HLP vuot qua %u byte, bo qua toi '\\n' ke tiep", (unsigned)sizeof(s_line_buf));
            } else {
                s_line_buf[s_line_len++] = c;
            }
        }
    }
}

// TX (write, Android -> board) va Capabilities (read) dung chung 1 callback,
// phan biet bang ctxt->op - giong dung code mau Document 4.
static int access_cb(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        size_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > HLP_MAX_FRAME) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        ble_chunk_t chunk = { .length = (uint16_t)length };
        if (os_mbuf_copydata(ctxt->om, 0, length, chunk.bytes) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (xQueueSend(s_chunks, &chunk, 0) != pdTRUE) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        static const char caps[] = "{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}\n";
        return os_mbuf_append(ctxt->om, caps, sizeof(caps) - 1) == 0
                ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// RX chi dung de NOTIFY (khong co property READ/WRITE) nen access_cb hau
// nhu khong bao gio duoc goi qua ATT that su, nhung NimBLE ban di kem
// ESP-IDF 5.5.5 van doi hoi access_cb khac NULL cho MOI characteristic
// (ble_gatts_chr_is_sane) - thieu no lam ble_gatts_count_cfg loi rc=3.
static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = &s_tx_uuid.u,
        .access_cb = access_cb,
        .flags = BLE_GATT_CHR_F_WRITE, // "write with response" theo yeu cau Document 9
    },
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = rx_access_cb,
        .val_handle = &s_rx_handle,
        .flags = BLE_GATT_CHR_F_NOTIFY, // CCCD 0x2902 duoc NimBLE tu tao
    },
    {
        .uuid = &s_caps_uuid.u,
        .access_cb = access_cb,
        .flags = BLE_GATT_CHR_F_READ,
    },
    { 0 },
};

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = s_characteristics,
    },
    { 0 },
};

static void start_advertise(void)
{
    // UUID 128-bit khong du cho vua chung ten thiet bi trong 1 goi adv 31
    // byte, nen dat rieng trong scan-response.
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields loi rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields loi rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start loi rc=%d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertise();
        } else {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Dien thoai da ket noi BLE (conn_handle=%u)", s_conn_handle);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Dien thoai ngat ket noi BLE: reason=%d (%s)",
                 event->disconnect.reason, hci_disconnect_reason_str(event->disconnect.reason));
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_rx_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Subscribe RX: cur_notify=%d", event->subscribe.cur_notify);
            if (s_notify_enabled) {
                // Gui "dev" ngay sau khi notify duoc bat (Document 9: bat tay HLP).
                s_send_dev_pending = true;
            }
        }
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%u mtu=%u", event->mtu.conn_handle, event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void)
{
    // Dia chi BLE CO DINH (khong random moi boot) - Waze mod luu chinh xac
    // dia chi Bluetooth cua thiet bi da chon de tu ket noi lai (Document 9:
    // "Chon thiet bi"). Dia chi doi moi lan boot se khien app luon thay day
    // la "thiet bi moi", khong bao gio reconnect duoc thiet bi cu. Dung dung
    // ham suy dia chi tu dong nhu code mau Document 4 (khong random).
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto loi rc=%d", rc);
        return;
    }

    start_advertise();
    ESP_LOGI(TAG, "Dang phat BLE Waze HUD Link voi ten '%s'", DEVICE_NAME);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Ghep frame LF va parse JSON chay o day (khong o trong GATT callback), theo
// dung khuyen nghi cua Document 2/4.
static void protocol_task(void *arg)
{
    (void)arg;
    ble_chunk_t chunk;
    for (;;) {
        if (s_send_dev_pending) {
            s_send_dev_pending = false;
            // want.fields chi liet ke nhung gi UI hien tai thuc su dung
            // (spd/lim) - mo rong khi UI ho tro them truong khac.
            send_line("{\"v\":1,\"t\":\"dev\",\"name\":\"ESP32-WazeHUD\",\"fw\":\"1.0.0\","
                      "\"proto\":[1],\"want\":{\"rate\":4,\"fields\":[\"spd\",\"lim\"]}}");
        }
        if (xQueueReceive(s_chunks, &chunk, pdMS_TO_TICKS(100)) == pdTRUE && chunk.length > 0) {
            feed_bytes(chunk.bytes, chunk.length);
        }
    }
}

esp_err_t waze_hud_ble_start(waze_hud_data_cb_t cb, void *ctx)
{
    s_data_cb = cb;
    s_cb_ctx = ctx;
    s_nav_cb = NULL;
    s_nav_cb_ctx = NULL;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    s_line_len = 0;
    s_line_overflow = false;

    s_chunks = xQueueCreate(16, sizeof(ble_chunk_t));
    if (!s_chunks) {
        ESP_LOGE(TAG, "Khong tao duoc hang doi nhan du lieu BLE");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init that bai: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg loi rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs loi rc=%d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_device_name_set(DEVICE_NAME);

    if (xTaskCreate(protocol_task, "hlp_protocol", 4096, NULL, 5, &s_protocol_task) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc protocol task");
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

void waze_hud_ble_stop(void)
{
    if (s_protocol_task) {
        vTaskDelete(s_protocol_task);
        s_protocol_task = NULL;
    }
    nimble_port_stop();
    nimble_port_deinit();
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    if (s_chunks) {
        vQueueDelete(s_chunks);
        s_chunks = NULL;
    }
}

bool waze_hud_ble_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void waze_hud_ble_set_nav_cb(waze_hud_nav_cb_t cb, void *ctx)
{
    s_nav_cb = cb;
    s_nav_cb_ctx = ctx;
}

void waze_hud_ble_set_vehicle_cb(waze_hud_vehicle_cb_t cb, void *ctx)
{
    s_vehicle_cb = cb;
    s_vehicle_cb_ctx = ctx;
}
