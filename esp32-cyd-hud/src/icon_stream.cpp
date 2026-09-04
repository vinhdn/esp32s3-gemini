// icon_stream.cpp - nhan 1 frame JPEG ghep 2 icon canh bao (trai/phai) qua
// BLE (chunked), giai ma bang TJpg_Decoder (khong co ROM tjpgd nhu S3, board
// nay cung khong co PSRAM nen dung buffer tinh nho trong SRAM thuong), tach
// doi va ve vao 2 canvas cua hud_ui (warn_icon_canvas[]).
//
// Luong xu ly (bam sat esp32/main/display/img_stream.c ben board S3 cu, chi
// doi backend giai ma):
//   ble_server.cpp (thay 0xFF dau) -> icon_stream_feed() -> ghep chunk vao
//   buffer tinh -> du 1 frame (SOI..EOI) -> bao task giai ma qua
//   task-notify -> TJpgDec.drawJpg() + callback tach nua trai/phai, scale
//   nearest-neighbor cho vua HUD_WARNING_ICON_SIZE -> hud_set_warning_icon_image().

#include "icon_stream.h"

#include <Arduino.h>
#include <TJpg_Decoder.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "hud_ui.h"

static const size_t ICON_MAX_JPEG_SIZE = 2048; // anh nguon that (80x40 q=30) ~1-2KB, du du

// ---- Ghep chunk (chay trong context BLE - onWrite) ----
// Cap phat HEAP (malloc trong icon_stream_init), KHONG phai static array -
// board nay khong PSRAM, DRAM tinh (BSS) rat han hep va da gan day boi
// Arduino core/NimBLE/LVGL; buffer heap khong bi tinh vao gioi han link-time
// nay (da xac nhan qua that bai build that: static array o day + hud_ui.c
// lam DRAM tran ~17.6KB, chuyen sang heap la du).
static uint8_t *s_assembly_buf;
static size_t s_assembly_size = 0;
static uint8_t s_prev_byte = 0;
static bool s_assembly_active = false;

// ---- Frame san sang cho task giai ma ----
static uint8_t *s_decode_buf;
static size_t s_decode_size = 0;
static volatile bool s_frame_ready = false;
// true trong SUOT qua trinh decode_task dang doc s_decode_buf - frame moi
// den luc nay bi bo qua (chap nhan mat frame) thay vi ghi de giua chung.
static volatile bool s_decoding = false;

static TaskHandle_t s_decode_task = nullptr;

// Anh nguon la 2 icon ghep canh nhau CUNG RONG (composeAlertIcons: 40x40 moi
// ben, 80x40 tong) - hardcode theo dung wire format hien tai thay vi tu do
// giai ma kich thuoc (don gian hon, khop chinh xac protocol dang dung).
static const uint16_t SRC_HALF_W = 40;
static const float SCALE = (float)HUD_WARNING_ICON_SIZE / (float)SRC_HALF_W;

static uint16_t *s_out_left;
static uint16_t *s_out_right;
static const size_t ICON_PIXELS = (size_t)HUD_WARNING_ICON_SIZE * HUD_WARNING_ICON_SIZE;

// TJpg_Decoder callback: nhan 1 khoi w x h pixel RGB565 tai vi tri (x,y)
// trong anh nguon (goc tren-trai 0,0) - tach trai/phai theo x, scale nearest-
// neighbor cho vua khung vuong HUD_WARNING_ICON_SIZE.
static bool tjpg_output_cb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data)
{
    for (uint16_t row = 0; row < h; row++) {
        int16_t sy = y + row;
        for (uint16_t col = 0; col < w; col++) {
            int16_t sx = x + col;
            uint16_t pixel = data[(size_t)row * w + col];

            bool is_right = sx >= (int16_t)SRC_HALF_W;
            int16_t local_sx = is_right ? (int16_t)(sx - SRC_HALF_W) : sx;
            uint16_t *buf = is_right ? s_out_right : s_out_left;

            int dxStart = (int)(local_sx * SCALE);
            int dxEnd = (int)((local_sx + 1) * SCALE);
            if (dxEnd <= dxStart) dxEnd = dxStart + 1;
            int dyStart = (int)(sy * SCALE);
            int dyEnd = (int)((sy + 1) * SCALE);
            if (dyEnd <= dyStart) dyEnd = dyStart + 1;

            for (int dy = dyStart; dy < dyEnd; dy++) {
                if (dy < 0 || dy >= HUD_WARNING_ICON_SIZE) continue;
                uint16_t *rowbuf = buf + (size_t)dy * HUD_WARNING_ICON_SIZE;
                for (int dx = dxStart; dx < dxEnd; dx++) {
                    if (dx < 0 || dx >= HUD_WARNING_ICON_SIZE) continue;
                    rowbuf[dx] = pixel;
                }
            }
        }
    }
    return true; // tiep tuc decode
}

static void decode_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_frame_ready) continue;
        s_frame_ready = false;
        s_decoding = true;

        size_t size = s_decode_size;
        if (size > 0) {
            memset(s_out_left, 0, ICON_PIXELS * sizeof(uint16_t));
            memset(s_out_right, 0, ICON_PIXELS * sizeof(uint16_t));
            JRESULT res = TJpgDec.drawJpg(0, 0, s_decode_buf, size);
            s_decoding = false; // xong doc s_decode_buf, cho phep frame moi ghi de

            Serial.printf("[icon_stream] TJpgDec.drawJpg xong, res=%d (JDR_OK=%d)\n", (int)res, (int)JDR_OK);

            if (res == JDR_OK) {
                Serial.printf("[icon_stream] Decode OK - mau px[0] trai=0x%04X phai=0x%04X - ve len canvas\n",
                              s_out_left[0], s_out_right[0]);
                // LEFT nua anh nguon = bien bao gioi han sap toi
                // (upcoming_alert_left, xem VietmapAccessibilityService.kt) -
                // trong ui.cpp slot 1 la next_limit, slot 0 la camera.
                hud_set_warning_icon_image(1, s_out_left);
                hud_set_warning_icon_image(0, s_out_right);
            }
        } else {
            s_decoding = false;
        }
    }
}

bool icon_stream_feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];

        // SOI (0xFF 0xD8): bat dau frame moi, reset buffer.
        if (s_prev_byte == 0xFF && b == 0xD8) {
            s_assembly_size = 0;
            s_assembly_buf[s_assembly_size++] = 0xFF;
            s_assembly_buf[s_assembly_size++] = 0xD8;
            s_assembly_active = true;
            s_prev_byte = b;
            Serial.println("[icon_stream] SOI nhan duoc - bat dau ghep frame moi");
            continue;
        }

        if (!s_assembly_active) {
            s_prev_byte = b;
            continue;
        }

        if (s_assembly_size < ICON_MAX_JPEG_SIZE) {
            s_assembly_buf[s_assembly_size++] = b;
        } else {
            s_assembly_active = false; // buffer day - huy frame
            s_prev_byte = b;
            continue;
        }

        // EOI (0xFF 0xD9): frame hoan chinh.
        if (s_prev_byte == 0xFF && b == 0xD9) {
            Serial.printf("[icon_stream] EOI nhan duoc, %u byte, frame_ready=%d decoding=%d\n",
                          (unsigned)s_assembly_size, (int)s_frame_ready, (int)s_decoding);
            if (!s_frame_ready && !s_decoding && s_assembly_size > 100) {
                memcpy(s_decode_buf, s_assembly_buf, s_assembly_size);
                s_decode_size = s_assembly_size;
                s_frame_ready = true;
                if (s_decode_task) xTaskNotifyGive(s_decode_task);
            } else {
                Serial.println("[icon_stream] Frame BI BO QUA (dang ban giai ma hoac qua nho)");
            }
            s_assembly_active = false;
        }

        s_prev_byte = b;
    }
    return s_assembly_active;
}

void icon_stream_init()
{
    s_assembly_buf = (uint8_t *)malloc(ICON_MAX_JPEG_SIZE);
    s_decode_buf = (uint8_t *)malloc(ICON_MAX_JPEG_SIZE);
    s_out_left = (uint16_t *)malloc(ICON_PIXELS * sizeof(uint16_t));
    s_out_right = (uint16_t *)malloc(ICON_PIXELS * sizeof(uint16_t));
    if (!s_assembly_buf || !s_decode_buf || !s_out_left || !s_out_right) {
        Serial.println("[icon_stream] Khong cap phat duoc buffer - tinh nang icon canh bao that TAT");
        return;
    }

    TJpgDec.setJpgScale(1);
    // KHONG swap byte o day: canvas nay la bo dem LVGL (LV_COLOR_FORMAT_RGB565)
    // giong het moi noi LVGL ve khac trong app - swap CHI xay ra 1 lan duy
    // nhat o disp_flush_cb (pushColors(...,true)) cho toan bo khung hinh da
    // ghep. Neu mau icon that bi sai/dao khi test that, doi thanh true o day
    // (chua kiem chung truc tiep duoc vi can 1 frame JPEG that tu dien thoai
    // luc dang lai xe moi kich hoat duong nay).
    TJpgDec.setCallback(tjpg_output_cb);

    xTaskCreatePinnedToCore(decode_task, "icon_decode", 4096, nullptr, 2, &s_decode_task, 1);
}
