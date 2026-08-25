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
    uint16_t chunk_sizes[IMG_MAX_CHUNKS]; // size of each chunk's data payload
    uint16_t chunk_offsets[IMG_MAX_CHUNKS]; // byte offset in jpeg_buf
    uint8_t total_chunks;       // expected total from header
    uint8_t received_mask[(IMG_MAX_CHUNKS + 7) / 8]; // bitmask of received chunks
    uint8_t received_count;     // how many chunks received so far
    uint8_t frame_id;           // current frame being assembled
    uint32_t total_size;        // total bytes of JPEG data accumulated
    bool active;                // assembly in progress
} frame_assembly_t;

// ------- Module state -------
static frame_assembly_t s_assembly;
static SemaphoreHandle_t s_assembly_mutex;  // protects s_assembly from BLE + task

// Double buffer: one for assembling, one ready for decode
static uint8_t *s_decode_buf;               // PSRAM buffer for decode input
static uint32_t s_decode_size;              // size of JPEG in decode buffer
static volatile bool s_frame_ready;         // signal to decode task

static TaskHandle_t s_decode_task;

// LVGL canvas and buffer
static lv_obj_t *s_canvas;
static uint8_t *s_canvas_buf;              // PSRAM RGB565 buffer (240*240*2 = 115200 bytes)

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

// Output function: converts RGB888 -> RGB565 and writes to canvas buffer
static UINT jpeg_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    uint8_t *rgb888 = (uint8_t *)bitmap;
    uint16_t *canvas_pixels = (uint16_t *)s_canvas_buf;

    uint16_t left = rect->left;
    uint16_t top = rect->top;
    uint16_t right = rect->right;
    uint16_t bottom = rect->bottom;

    // Clip to canvas bounds
    if (right >= IMG_WIDTH) right = IMG_WIDTH - 1;
    if (bottom >= IMG_HEIGHT) bottom = IMG_HEIGHT - 1;

    for (uint16_t y = top; y <= bottom; y++) {
        for (uint16_t x = left; x <= right; x++) {
            // RGB888 source pixel
            uint8_t r = *rgb888++;
            uint8_t g = *rgb888++;
            uint8_t b = *rgb888++;

            // Convert to RGB565
            uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            canvas_pixels[y * IMG_WIDTH + x] = pixel;
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

        uint32_t size = s_decode_size;
        if (size == 0) {
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

        // Decompress with output callback
        res = jd_decomp(&jdec, jpeg_output_func, scale);
        if (res != JDR_OK) {
            ESP_LOGW(TAG, "jd_decomp loi: %d", res);
            continue;
        }

        // Invalidate LVGL canvas so it redraws
        if (lvgl_port_lock(100)) {
            lv_obj_invalidate(s_canvas);
            lvgl_port_unlock();
        }

        ESP_LOGI(TAG, "Frame giai ma va hien thi thanh cong");
    }
}

// ------- Frame assembly (called from BLE context) -------

static inline void set_chunk_bit(uint8_t *mask, uint8_t idx)
{
    mask[idx / 8] |= (1 << (idx % 8));
}

static inline bool get_chunk_bit(const uint8_t *mask, uint8_t idx)
{
    return (mask[idx / 8] & (1 << (idx % 8))) != 0;
}

void img_stream_feed_chunk(const uint8_t *data, uint16_t len)
{
    if (!s_initialized || len < 3) {
        return;
    }

    if (xSemaphoreTake(s_assembly_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "feed_chunk: khong lay duoc mutex, bo chunk");
        return;
    }

    // Detect first chunk of a new frame: starts with [0xFF, 0xD8, frame_id, total_chunks]
    if (len >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        uint8_t frame_id = data[2];
        uint8_t total_chunks = data[3];

        if (total_chunks == 0 || total_chunks > IMG_MAX_CHUNKS) {
            ESP_LOGW(TAG, "Frame %u: total_chunks=%u khong hop le", frame_id, total_chunks);
            xSemaphoreGive(s_assembly_mutex);
            return;
        }

        // Start new frame (discard any in-progress assembly)
        memset(&s_assembly.received_mask, 0, sizeof(s_assembly.received_mask));
        s_assembly.frame_id = frame_id;
        s_assembly.total_chunks = total_chunks;
        s_assembly.received_count = 0;
        s_assembly.total_size = 0;
        s_assembly.active = true;

        // Chunk 0 data starts after 4-byte header
        uint16_t payload_len = len - 4;
        if (payload_len > 0 && s_assembly.total_size + payload_len <= IMG_MAX_JPEG_SIZE) {
            s_assembly.chunk_offsets[0] = 0;
            s_assembly.chunk_sizes[0] = payload_len;
            memcpy(s_assembly.jpeg_buf, data + 4, payload_len);
            s_assembly.total_size = payload_len;
            set_chunk_bit(s_assembly.received_mask, 0);
            s_assembly.received_count = 1;
        }
    } else if (s_assembly.active && len >= 2) {
        // Subsequent chunk: [frame_id, chunk_index, ...data...]
        uint8_t frame_id = data[0];
        uint8_t chunk_index = data[1];

        if (frame_id != s_assembly.frame_id) {
            // Chunk for a different/old frame, ignore
            xSemaphoreGive(s_assembly_mutex);
            return;
        }
        if (chunk_index >= s_assembly.total_chunks || chunk_index == 0) {
            // Invalid index or duplicate first chunk marker
            xSemaphoreGive(s_assembly_mutex);
            return;
        }
        if (get_chunk_bit(s_assembly.received_mask, chunk_index)) {
            // Already received this chunk, ignore duplicate
            xSemaphoreGive(s_assembly_mutex);
            return;
        }

        uint16_t payload_len = len - 2;
        if (payload_len > 0 && s_assembly.total_size + payload_len <= IMG_MAX_JPEG_SIZE) {
            s_assembly.chunk_offsets[chunk_index] = (uint16_t)s_assembly.total_size;
            s_assembly.chunk_sizes[chunk_index] = payload_len;
            memcpy(s_assembly.jpeg_buf + s_assembly.total_size, data + 2, payload_len);
            s_assembly.total_size += payload_len;
            set_chunk_bit(s_assembly.received_mask, chunk_index);
            s_assembly.received_count++;
        } else {
            ESP_LOGW(TAG, "Frame %u: vuot qua buffer %u bytes", frame_id, IMG_MAX_JPEG_SIZE);
        }
    } else {
        xSemaphoreGive(s_assembly_mutex);
        return;
    }

    // Check if frame is complete
    if (s_assembly.active && s_assembly.received_count >= s_assembly.total_chunks) {
        // Reassemble in order: copy chunks sequentially into decode buffer
        uint32_t offset = 0;
        for (uint8_t i = 0; i < s_assembly.total_chunks && offset < IMG_MAX_JPEG_SIZE; i++) {
            uint16_t sz = s_assembly.chunk_sizes[i];
            uint16_t src_off = s_assembly.chunk_offsets[i];
            if (sz > 0 && offset + sz <= IMG_MAX_JPEG_SIZE) {
                memcpy(s_decode_buf + offset, s_assembly.jpeg_buf + src_off, sz);
                offset += sz;
            }
        }
        s_decode_size = offset;
        s_assembly.active = false;
        s_frame_ready = true;

        // Notify decode task
        if (s_decode_task) {
            xTaskNotifyGive(s_decode_task);
        }

        ESP_LOGD(TAG, "Frame %u hoan chinh: %lu bytes tu %u chunks",
                 s_assembly.frame_id, (unsigned long)offset, s_assembly.total_chunks);
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

    // Create LVGL canvas object
    if (lvgl_port_lock(200)) {
        s_canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(s_canvas, s_canvas_buf, IMG_WIDTH, IMG_HEIGHT,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_center(s_canvas);
        // Start hidden - caller can show when needed
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        // Fill with black initially
        lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);
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
