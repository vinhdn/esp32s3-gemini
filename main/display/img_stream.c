// img_stream.c — Nhan anh JPEG qua BLE (chunked), giai ma bang tjpgd trong
// ROM ESP32-S3, hien thi tren LVGL canvas 240x240 RGB565.
//
// Luong xu ly:
//   BLE callback -> img_stream_feed_chunk() -> ghep chunk vao buffer PSRAM
//   -> khi du frame, gui notify cho decode task -> jd_prepare + jd_decomp
//   -> output callback chuyen RGB888 -> RGB565 truc tiep vao canvas buffer
//   -> lvgl_port_lock + lv_obj_invalidate -> LVGL tu ve ra LCD.

#include "img_stream.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp32s3/rom/tjpgd.h"

#include "board_config.h"

static const char *TAG = "img_stream";

// ------- Configuration -------
#define IMG_MAX_JPEG_SIZE    (20 * 1024)   // Max 20KB JPEG per frame
#define IMG_MAX_CHUNKS       64            // Max chunks per frame
#define IMG_DECODE_WORK_SIZE 3100          // tjpgd workspace (>=JD_SZBUF + tables)
#define IMG_WIDTH            BOARD_LCD_H_RES  // 240
#define IMG_HEIGHT           BOARD_LCD_V_RES  // 240

// ------- Frame assembly state (accessed from BLE context) -------
typedef struct {
    uint8_t *jpeg_buf;          // PSRAM buffer for assembled JPEG data
    uint32_t total_size;        // total bytes of JPEG data accumulated
    uint8_t prev_byte;          // byte trước (để detect 0xFF 0xD8 / 0xFF 0xD9)
    bool active;                // assembly in progress (đang giữa SOI và EOI)
} frame_assembly_t;

// ------- Module state -------
static frame_assembly_t s_assembly;
static SemaphoreHandle_t s_assembly_mutex;  // protects s_assembly from BLE + task

// Double buffer: one for assembling, one ready for decode
static uint8_t *s_decode_buf;               // PSRAM buffer for decode input
static uint32_t s_decode_size;              // size of JPEG in decode buffer
static volatile bool s_frame_ready;         // signal to decode task
// true trong SUOT qua trinh decode_task doc s_decode_buf (jd_prepare..jd_decomp),
// khong chi luc cho duoc pick up. Thieu co nay truoc day gay race: BLE task co
// the memcpy de len s_decode_buf NGAY TRONG LUC decode_task dang doc no (vi
// s_frame_ready da bi set false tu dau vong lap, truoc khi decode xong), lam
// frame dang giai ma bi rach/hong hinh. Chap nhan mat frame (theo yeu cau) thay
// vi de bi race: frame moi den luc dang decode se bi bo qua o img_stream_feed_chunk().
static volatile bool s_decoding;

static TaskHandle_t s_decode_task;

// LVGL canvas and buffer
static lv_obj_t *s_canvas;
static uint8_t *s_canvas_buf;              // PSRAM RGB565 buffer (240*240*2 = 115200 bytes)

// Label "Loading..." hien khi chua nhan duoc frame bong bong nao - an vinh
// vien sau frame dau tien giai ma thanh cong.
static lv_obj_t *s_loading_label;
static bool s_first_frame_shown;

// Cham nho goc tren-phai bao trang thai ket noi BLE - dau hieu ket noi toi
// thieu sau khi da xoa toan bo UI toc do (xem app_main.c/ui_screens.c).
static lv_obj_t *s_conn_dot;

static void *s_tjpgd_work;                 // PSRAM workspace for tjpgd

static bool s_initialized;

// ------- tjpgd callbacks -------

// Input function: reads JPEG data from decode buffer
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
} jpeg_io_t;

static UINT jpeg_input_func(JDEC *jd, BYTE *buff, UINT ndata)
{
    jpeg_io_t *io = (jpeg_io_t *)jd->device;
    uint32_t remain = io->size - io->pos;
    if (ndata > remain) {
        ndata = (UINT)remain;
    }
    if (buff) {
        memcpy(buff, io->data + io->pos, ndata);
    }
    io->pos += ndata;
    return ndata;
}

// Vi tri dat anh da giai ma len canvas 240x240 - anh Android gui gio la
// ty le that (vd 16:9, KHONG con bi Android tu dem vien den cho vua khung
// vuong nhu truoc). Tinh lai truoc moi lan jd_decomp() trong decode_task().
// Neo duoi + can giua ngang de anh nam duoi vung 2 vong tron speed (y~42-154)
// thay vi bi che gan het nhu khi dat giua man hinh.
static int16_t s_dst_x;
static int16_t s_dst_y;

// Output function: converts RGB888 -> RGB565 and writes to canvas buffer
static UINT jpeg_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    uint8_t *rgb888 = (uint8_t *)bitmap;
    uint16_t *canvas_pixels = (uint16_t *)s_canvas_buf;

    for (uint16_t sy = rect->top; sy <= rect->bottom; sy++) {
        int dy = (int)sy + s_dst_y;
        if (dy < 0 || dy >= IMG_HEIGHT) {
            rgb888 += 3 * (rect->right - rect->left + 1);
            continue;
        }
        for (uint16_t sx = rect->left; sx <= rect->right; sx++) {
            // RGB888 source pixel
            uint8_t r = *rgb888++;
            uint8_t g = *rgb888++;
            uint8_t b = *rgb888++;

            int dx = (int)sx + s_dst_x;
            if (dx < 0 || dx >= IMG_WIDTH) {
                continue;
            }

            // Convert to RGB565
            uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            canvas_pixels[dy * IMG_WIDTH + dx] = pixel;
        }
    }

    return 1; // Continue decompression
}

// ------- Decode task -------

static void decode_task(void *arg)
{
    (void)arg;
    JDEC jdec;
    jpeg_io_t io;

    for (;;) {
        // Wait for a frame to be ready
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!s_frame_ready) {
            continue;
        }
        s_frame_ready = false;
        s_decoding = true;

        uint32_t size = s_decode_size;
        if (size == 0) {
            s_decoding = false;
            continue;
        }

        ESP_LOGI(TAG, "Giai ma JPEG frame: %lu bytes", (unsigned long)size);

        // Setup IO struct for tjpgd input callback
        io.data = s_decode_buf;
        io.size = size;
        io.pos = 0;

        // Prepare JPEG decode
        JRESULT res = jd_prepare(&jdec, jpeg_input_func, s_tjpgd_work,
                                  IMG_DECODE_WORK_SIZE, &io);
        if (res != JDR_OK) {
            ESP_LOGW(TAG, "jd_prepare loi: %d (JPEG co the bi loi/khong ho tro)", res);
            s_decoding = false;
            continue;
        }

        ESP_LOGI(TAG, "JPEG: %ux%u", jdec.width, jdec.height);

        // Determine scale factor to fit 240x240
        // Scale: 0=1/1, 1=1/2, 2=1/4, 3=1/8
        uint8_t scale = 0;
        uint32_t w = jdec.width;
        uint32_t h = jdec.height;
        while (scale < 3 && (w > IMG_WIDTH * 2 || h > IMG_HEIGHT * 2)) {
            scale++;
            w /= 2;
            h /= 2;
        }
        if (w > IMG_WIDTH || h > IMG_HEIGHT) {
            // Still too big at current scale, try one more
            if (scale < 3) {
                scale++;
            }
        }

        // Anh (bitmap bong bong ghep tu Android) khong con phai ne 2 vong
        // tron speed nua (da xoa UI toc do) - can giua ca ngang lan doc.
        // Xoa canvas ve den truoc de khong dinh anh frame truoc (vi tri/kich
        // thuoc co the doi giua cac frame).
        s_dst_x = (int16_t)((IMG_WIDTH - (int)w) / 2);
        s_dst_y = (int16_t)((IMG_HEIGHT - (int)h) / 2);
        if (s_dst_x < 0) s_dst_x = 0;
        if (s_dst_y < 0) s_dst_y = 0;
        memset(s_canvas_buf, 0, (size_t)IMG_WIDTH * IMG_HEIGHT * sizeof(uint16_t));

        // Decompress with output callback
        res = jd_decomp(&jdec, jpeg_output_func, scale);
        // Từ đây trở đi không còn đọc s_decode_buf nữa - cho phép frame mới
        // ghi đè ngay cả khi phần hiển thị (LVGL invalidate) bên dưới còn chạy.
        s_decoding = false;
        if (res != JDR_OK) {
            ESP_LOGW(TAG, "jd_decomp loi: %d", res);
            continue;
        }

        // Invalidate LVGL canvas so it redraws, và ẩn vĩnh viễn label
        // "Loading..." sau khung hình đầu tiên hiển thị thành công.
        if (lvgl_port_lock(100)) {
            lv_obj_invalidate(s_canvas);
            if (!s_first_frame_shown) {
                s_first_frame_shown = true;
                if (s_loading_label) {
                    lv_obj_add_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
                }
            }
            lvgl_port_unlock();
        }

        ESP_LOGI(TAG, "Frame giai ma va hien thi thanh cong");
    }
}

// ------- Frame assembly (called from BLE context) -------

void img_stream_feed_chunk(const uint8_t *data, uint16_t len)
{
    if (!s_initialized || len < 1) {
        return;
    }

    if (xSemaphoreTake(s_assembly_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    // Raw JPEG stream: detect SOI (0xFF 0xD8) = frame start, EOI (0xFF 0xD9) = frame end.
    // Android gửi JPEG chia thành các chunk 244 bytes tuần tự, không có header.
    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        // Detect SOI marker: 0xFF 0xD8 → bắt đầu frame mới
        if (s_assembly.prev_byte == 0xFF && b == 0xD8) {
            // Reset buffer, ghi lại 0xFF 0xD8
            s_assembly.total_size = 0;
            if (s_assembly.total_size + 2 <= IMG_MAX_JPEG_SIZE) {
                s_assembly.jpeg_buf[s_assembly.total_size++] = 0xFF;
                s_assembly.jpeg_buf[s_assembly.total_size++] = 0xD8;
            }
            s_assembly.active = true;
            s_assembly.prev_byte = b;
            continue;
        }

        if (!s_assembly.active) {
            s_assembly.prev_byte = b;
            continue;
        }

        // Ghi byte vào buffer
        if (s_assembly.total_size < IMG_MAX_JPEG_SIZE) {
            s_assembly.jpeg_buf[s_assembly.total_size++] = b;
        } else {
            // Buffer overflow → hủy frame
            s_assembly.active = false;
            s_assembly.prev_byte = b;
            continue;
        }

        // Detect EOI marker: 0xFF 0xD9 → kết thúc frame
        if (s_assembly.prev_byte == 0xFF && b == 0xD9) {
            // Frame hoàn chỉnh → copy sang decode buffer, NHƯNG chỉ khi
            // decode_task không đang đọc s_decode_buf (s_decoding) và không
            // có frame khác đang chờ pick up (s_frame_ready). Nếu board đang
            // bận giải mã frame trước, bỏ qua frame này luôn (chấp nhận mất
            // frame) thay vì memcpy đè lên buffer đang được đọc — trước đây
            // thiếu check s_decoding nên có thể ghi đè GIỮA LÚC decode_task
            // đang đọc, làm ảnh bị rách/hỏng.
            if (!s_frame_ready && !s_decoding && s_assembly.total_size > 100) {
                memcpy(s_decode_buf, s_assembly.jpeg_buf, s_assembly.total_size);
                s_decode_size = s_assembly.total_size;
                s_frame_ready = true;
                if (s_decode_task) {
                    xTaskNotifyGive(s_decode_task);
                }
            }
            s_assembly.active = false;
        }

        s_assembly.prev_byte = b;
    }

    xSemaphoreGive(s_assembly_mutex);
}

// ------- Public API -------

esp_err_t img_stream_init(lv_obj_t *parent)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Allocate JPEG receive buffer in PSRAM
    s_assembly.jpeg_buf = heap_caps_calloc(1, IMG_MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_assembly.jpeg_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc JPEG receive buffer (%d bytes PSRAM)", IMG_MAX_JPEG_SIZE);
        return ESP_ERR_NO_MEM;
    }

    // Allocate decode buffer in PSRAM
    s_decode_buf = heap_caps_calloc(1, IMG_MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_decode_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc decode buffer (%d bytes PSRAM)", IMG_MAX_JPEG_SIZE);
        return ESP_ERR_NO_MEM;
    }

    // Allocate tjpgd workspace in PSRAM
    s_tjpgd_work = heap_caps_calloc(1, IMG_DECODE_WORK_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_tjpgd_work) {
        ESP_LOGE(TAG, "Khong cap phat duoc tjpgd workspace (%d bytes PSRAM)", IMG_DECODE_WORK_SIZE);
        return ESP_ERR_NO_MEM;
    }

    // Allocate canvas framebuffer in PSRAM (240*240*2 = 115200 bytes RGB565)
    size_t canvas_buf_size = IMG_WIDTH * IMG_HEIGHT * sizeof(uint16_t);
    s_canvas_buf = heap_caps_calloc(1, canvas_buf_size, MALLOC_CAP_SPIRAM);
    if (!s_canvas_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc canvas buffer (%u bytes PSRAM)", (unsigned)canvas_buf_size);
        return ESP_ERR_NO_MEM;
    }

    // Create mutex for frame assembly
    s_assembly_mutex = xSemaphoreCreateMutex();
    if (!s_assembly_mutex) {
        ESP_LOGE(TAG, "Khong tao duoc assembly mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create LVGL canvas object. Khong con UI toc do de len tren nua - canvas
    // (+ label loading) la NOI DUNG DUY NHAT tren man hinh.
    if (lvgl_port_lock(200)) {
        s_canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(s_canvas, s_canvas_buf, IMG_WIDTH, IMG_HEIGHT,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_center(s_canvas);
        // Fill with black initially - luon hien, khong con toggle full-screen
        lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

        // Label "Loading..." - tao SAU canvas nen mac dinh nam tren, an sau
        // khi nhan duoc frame bong bong dau tien (xem decode_task()).
        s_loading_label = lv_label_create(parent);
        lv_obj_set_style_text_color(s_loading_label, lv_color_white(), 0);
        lv_label_set_text(s_loading_label, "Loading...");
        lv_obj_center(s_loading_label);

        // Cham trang thai ket noi BLE - goc tren-phai, nho (10x10) de khong
        // che anh bong bong. Do mac dinh (chua ket noi) den khi
        // img_stream_set_connected(true) duoc goi.
        s_conn_dot = lv_obj_create(parent);
        lv_obj_remove_style_all(s_conn_dot);
        lv_obj_set_size(s_conn_dot, 10, 10);
        lv_obj_set_style_radius(s_conn_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_conn_dot, lv_color_hex(0xE02020), 0);
        lv_obj_set_style_bg_opa(s_conn_dot, LV_OPA_COVER, 0);
        lv_obj_align(s_conn_dot, LV_ALIGN_TOP_RIGHT, -4, 4);

        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Khong lock duoc LVGL de tao canvas");
        return ESP_FAIL;
    }

    // Create decode task (stack in PSRAM is ok for non-ISR task)
    BaseType_t ret = xTaskCreatePinnedToCore(
        decode_task, "img_decode", 4096, NULL, 4, &s_decode_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc img_decode task");
        return ESP_FAIL;
    }

    s_assembly.active = false;
    s_frame_ready = false;
    s_initialized = true;

    ESP_LOGI(TAG, "img_stream khoi tao xong (canvas %dx%d, JPEG buf %dKB)",
             IMG_WIDTH, IMG_HEIGHT, IMG_MAX_JPEG_SIZE / 1024);
    return ESP_OK;
}

void img_stream_show(bool visible)
{
    if (!s_initialized || !s_canvas) {
        return;
    }
    if (lvgl_port_lock(100)) {
        if (visible) {
            lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    }
}

bool img_stream_is_ready(void)
{
    return s_initialized;
}

void img_stream_set_connected(bool connected)
{
    if (!s_initialized || !s_conn_dot) {
        return;
    }
    if (lvgl_port_lock(100)) {
        lv_obj_set_style_bg_color(s_conn_dot,
            connected ? lv_color_hex(0x33CC66) : lv_color_hex(0xE02020), 0);
        lvgl_port_unlock();
    }
}
