// img_stream.c — Nhan 1 frame JPEG ghep 2 icon canh bao (trai/phai) qua BLE
// (chunked), giai ma bang tjpgd ROM trong ESP32-S3, tach doi va ve vao 2
// canvas TRON nho gan lam con cua next_limit_circle/camera_circle
// (ui_screens.c) — hien dung vi tri, dung mau nen/vien san co cua bong
// bong VietMap Live, thay vi che ca man hinh nhu ban demo dau tien.
//
// Luong xu ly:
//   BLE callback -> img_stream_feed_chunk() -> ghep chunk vao buffer PSRAM
//   -> khi du frame, gui notify cho decode task -> jd_prepare + jd_decomp
//   -> output callback: pixel nguon co sx < nua-trai -> canvas trai,
//      con lai -> canvas phai (tu dong scale de vua vong tron 52x52)
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

static const char *TAG = "img_stream";

// ------- Configuration -------
#define IMG_MAX_JPEG_SIZE    (20 * 1024)   // Max 20KB JPEG per frame
#define IMG_DECODE_WORK_SIZE 3100          // tjpgd workspace (>=JD_SZBUF + tables)
// Duong kinh vong tron icon - vong tron cha (next_limit_circle/
// camera_circle, ui_screens.c) la 60x60 vien 4px, chua 52x52 vua khop trong
// phan nen, khong de lo goc vuong ra ngoai vien mau.
#define ICON_CANVAS_SIZE 52

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

static uint8_t *s_decode_buf;               // PSRAM buffer for decode input
static uint32_t s_decode_size;              // size of JPEG in decode buffer
static volatile bool s_frame_ready;         // signal to decode task
// true trong SUOT qua trinh decode_task doc s_decode_buf (jd_prepare..jd_decomp).
// Frame moi den luc dang decode se bi bo qua o img_stream_feed_chunk() thay
// vi ghi de giua chung (rach anh) - chap nhan mat frame.
static volatile bool s_decoding;

static TaskHandle_t s_decode_task;

// 2 canvas TRON, moi cai la con cua 1 vong tron trong ui_screens.c.
static lv_obj_t *s_canvas_left;
static uint8_t *s_canvas_left_buf;
static lv_obj_t *s_canvas_right;
static uint8_t *s_canvas_right_buf;
// An ca 2 canvas cho toi khi co frame anh that dau tien - truoc do vong
// tron cha van hien binh thuong voi so/placeholder cua no (ui_screens.c).
static bool s_first_frame_shown;

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

// Anh nguon la 2 icon GHEP CANH NHAU, rong bang nhau (vd 80x40 -> moi ben
// 40x40) - s_src_half_w = rong 1 nua, s_scale = he so phong/thu de vua
// ICON_CANVAS_SIZE. Tinh lai truoc moi lan jd_decomp() trong decode_task().
static uint16_t s_src_half_w;
static float s_scale;

// Output function: converts RGB888 -> RGB565, tach trai/phai va phong to
// (nearest-neighbor) vao dung canvas.
static UINT jpeg_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    uint8_t *rgb888 = (uint8_t *)bitmap;

    for (uint16_t sy = rect->top; sy <= rect->bottom; sy++) {
        for (uint16_t sx = rect->left; sx <= rect->right; sx++) {
            uint8_t r = *rgb888++;
            uint8_t g = *rgb888++;
            uint8_t b = *rgb888++;
            uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

            bool is_right = sx >= s_src_half_w;
            uint16_t local_sx = is_right ? (uint16_t)(sx - s_src_half_w) : sx;
            uint16_t *buf = is_right ? (uint16_t *)s_canvas_right_buf
                                      : (uint16_t *)s_canvas_left_buf;

            int dxStart = (int)(local_sx * s_scale);
            int dxEnd = (int)((local_sx + 1) * s_scale);
            if (dxEnd <= dxStart) dxEnd = dxStart + 1;
            int dyStart = (int)(sy * s_scale);
            int dyEnd = (int)((sy + 1) * s_scale);
            if (dyEnd <= dyStart) dyEnd = dyStart + 1;

            for (int dy = dyStart; dy < dyEnd; dy++) {
                if (dy < 0 || dy >= ICON_CANVAS_SIZE) continue;
                uint16_t *row = buf + (size_t)dy * ICON_CANVAS_SIZE;
                for (int dx = dxStart; dx < dxEnd; dx++) {
                    if (dx < 0 || dx >= ICON_CANVAS_SIZE) continue;
                    row[dx] = pixel;
                }
            }
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

        ESP_LOGI(TAG, "Giai ma JPEG icon canh bao: %lu bytes", (unsigned long)size);

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

        ESP_LOGI(TAG, "JPEG icon: %ux%u", jdec.width, jdec.height);

        s_src_half_w = (uint16_t)(jdec.width / 2);
        if (s_src_half_w == 0) s_src_half_w = 1;
        s_scale = (float)ICON_CANVAS_SIZE / (float)s_src_half_w;

        memset(s_canvas_left_buf, 0, (size_t)ICON_CANVAS_SIZE * ICON_CANVAS_SIZE * sizeof(uint16_t));
        memset(s_canvas_right_buf, 0, (size_t)ICON_CANVAS_SIZE * ICON_CANVAS_SIZE * sizeof(uint16_t));

        // Decompress with output callback (scale=0: giai ma full-res, anh
        // nguon da rat nho - 80x40 - khong can tjpgd tu downscale).
        res = jd_decomp(&jdec, jpeg_output_func, 0);
        // Từ đây trở đi không còn đọc s_decode_buf nữa - cho phép frame mới
        // ghi đè ngay cả khi phần hiển thị (LVGL invalidate) bên dưới còn chạy.
        s_decoding = false;
        if (res != JDR_OK) {
            ESP_LOGW(TAG, "jd_decomp loi: %d", res);
            continue;
        }

        if (lvgl_port_lock(100)) {
            lv_obj_invalidate(s_canvas_left);
            lv_obj_invalidate(s_canvas_right);
            if (!s_first_frame_shown) {
                s_first_frame_shown = true;
                lv_obj_clear_flag(s_canvas_left, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(s_canvas_right, LV_OBJ_FLAG_HIDDEN);
            }
            lvgl_port_unlock();
        }

        ESP_LOGI(TAG, "Icon canh bao giai ma va hien thi thanh cong");
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
            // frame) thay vì memcpy đè lên buffer đang được đọc.
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

static lv_obj_t *create_icon_canvas(lv_obj_t *parent, uint8_t **buf_out)
{
    size_t buf_size = (size_t)ICON_CANVAS_SIZE * ICON_CANVAS_SIZE * sizeof(uint16_t);
    uint8_t *buf = heap_caps_calloc(1, buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return NULL;
    }
    *buf_out = buf;

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, ICON_CANVAS_SIZE, ICON_CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
    // Bo tron goc theo dung hinh dang vong tron cha - clip_corner khien
    // LVGL cat noi dung canvas (anh) theo radius, khong chi vien trang tri.
    lv_obj_set_style_radius(canvas, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(canvas, true, 0);
    lv_obj_center(canvas);
    // An cho toi khi co frame anh that dau tien.
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    return canvas;
}

esp_err_t img_stream_init(lv_obj_t *left_circle, lv_obj_t *right_circle)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!left_circle || !right_circle) {
        ESP_LOGE(TAG, "left_circle/right_circle NULL");
        return ESP_ERR_INVALID_ARG;
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

    // Create mutex for frame assembly
    s_assembly_mutex = xSemaphoreCreateMutex();
    if (!s_assembly_mutex) {
        ESP_LOGE(TAG, "Khong tao duoc assembly mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create the 2 round icon canvases (children of the alert circles)
    if (lvgl_port_lock(200)) {
        s_canvas_left = create_icon_canvas(left_circle, &s_canvas_left_buf);
        s_canvas_right = create_icon_canvas(right_circle, &s_canvas_right_buf);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Khong lock duoc LVGL de tao canvas icon");
        return ESP_FAIL;
    }
    if (!s_canvas_left || !s_canvas_right) {
        ESP_LOGE(TAG, "Khong cap phat duoc canvas buffer icon");
        return ESP_ERR_NO_MEM;
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

    ESP_LOGI(TAG, "img_stream khoi tao xong (2 canvas tron %dx%d, JPEG buf %dKB)",
             ICON_CANVAS_SIZE, ICON_CANVAS_SIZE, IMG_MAX_JPEG_SIZE / 1024);
    return ESP_OK;
}

bool img_stream_is_ready(void)
{
    return s_initialized;
}
