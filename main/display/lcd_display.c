// Khoi tao LCD ST7789 (SPI) + LVGL, theo dung trinh tu da kiem chung tren
// board LC-S3-WiFi-1.54TFT (SPI3_HOST, rgb_ele_order=RGB, invert_color=true).
//
// LUU Y: esp_lvgl_port la managed component thay doi API theo tung phien
// ban. Code duoi day viet theo API cua esp_lvgl_port ^2.8 (LVGL 9.x). Neu
// idf.py build bao loi thieu truong/enum trong lvgl_port_display_cfg_t, doi
// chieu voi README/example cua component (components/esp_lvgl_port trong
// managed_components/ sau khi `idf.py build` tai ve) va sua lai cho khop.

#include "lcd_display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lvgl_port.h"

#include "board_config.h"

static const char *TAG = "lcd_display";

static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_lvgl_disp = NULL;

#define BACKLIGHT_LEDC_TIMER   LEDC_TIMER_0
#define BACKLIGHT_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_RES     LEDC_TIMER_10_BIT // 0-1023
#define BACKLIGHT_LEDC_FREQ_HZ 5000

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .duty_resolution = BACKLIGHT_LEDC_RES,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num = BOARD_LCD_PIN_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    lcd_display_set_backlight_percent(BOARD_LCD_BACKLIGHT_DEFAULT_PERCENT);
}

void lcd_display_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t max_duty = (1 << BACKLIGHT_LEDC_RES) - 1;
    uint32_t duty = (max_duty * percent) / 100;
    if (BOARD_LCD_BACKLIGHT_INVERT) {
        duty = max_duty - duty;
    }
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}

esp_err_t lcd_display_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_SPI_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(BOARD_LCD_SPI_HOST, &io_config, &s_panel_io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = BOARD_LCD_RGB_ELEMENT_ORDER_IS_RGB ? LCD_RGB_ELEMENT_ORDER_RGB
                                                             : LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel));

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, BOARD_LCD_INVERT_COLOR);
    esp_lcd_panel_swap_xy(s_panel, BOARD_LCD_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    backlight_init();

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Buffer ve nho + 1 buffer (khong double-buffer) de tiet kiem RAM noi bo
    // DMA-capable — da xac nhan qua do thuc te tren board: buffer 40 dong x2
    // (double buffer) chiem ~38KB RAM noi bo, gay thieu RAM cho mbedtls luc
    // ket noi TLS (loi that gap phai: MBEDTLS_ERR_SSL_ALLOC_FAILED). Van con
    // quan trong voi kien truc hien tai: cac request HTTPS toi Groq cung can
    // RAM noi bo lien tuc cho mbedtls. UI chi la man hinh trang thai don gian,
    // khong can toc do ve nhanh nen doi hoi sinh (single buffer, 10 dong/lan)
    // hoan toan chap nhan duoc.
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .buffer_size = BOARD_LCD_H_RES * 10,
        .double_buffer = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = BOARD_LCD_SWAP_XY,
            .mirror_x = BOARD_LCD_MIRROR_X,
            .mirror_y = BOARD_LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_lvgl_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp that bai");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LCD + LVGL da khoi tao xong (%dx%d)", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

lv_display_t *lcd_display_get_lvgl_disp(void)
{
    return s_lvgl_disp;
}

void lcd_display_set_hud_flip(bool flipped)
{
    if (!s_panel) {
        return;
    }
    // MIRROR NGANG (trai-phai), KHONG PHAI xoay 180 do - xoay 180 se lat ca
    // truc doc (hien nguoc tren-duoi), khac voi anh guong that (chi lat
    // trai-phai, tren-duoi giu nguyen). Dung thang esp_lcd_panel_mirror() de
    // dieu khien MADCTL cua panel (ST7789) o tang phan cung - hoat dong doc
    // lap voi buffer/toa do cua LVGL, khong can invalidate/xoay gi them.
    // mirror_x XOR voi BOARD_LCD_MIRROR_X (huong GOC luc lcd_display_init(),
    // khong lien quan che do HUD) de bat/tat dung nghia; mirror_y giu
    // nguyen huong goc.
    esp_lcd_panel_mirror(s_panel, BOARD_LCD_MIRROR_X ^ flipped, BOARD_LCD_MIRROR_Y);
    ESP_LOGI(TAG, "HUD flip (mirror ngang): %s", flipped ? "BAT" : "TAT");
}
