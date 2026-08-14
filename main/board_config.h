#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/i2s_types.h"
#include "esp_adc/adc_oneshot.h"

// ============================================================================
// Pinout thuc te cua board "LC-S3-WiFi-1.54TFT" (ESP32-S3 + LCD ST7789 1.54"
// 10 pin + codec ES8311), doi chieu tu du lieu board board do nguoi dung cung
// cap (nhanh feature/add-lc-s3-154-board cua du an xiaozhi-esp32). Neu board
// that cua ban khac (vd revision PCB moi hon), sua lai cac define duoi day —
// day la noi DUY NHAT can sua GPIO cho toan bo firmware.
// ============================================================================

// ---- LCD ST7789 (SPI) ------------------------------------------------------
// Header 10 chan: VCC, GND, SCL(SCLK), SDA(MOSI), RES(RST), DC, CS, BLK, + 2
// chan du phong. Board dung SPI3_HOST (khong phai SPI2), mau RGB (khong phai
// BGR) va can dao mau (invert color) de hien thi dung — hai chi tiet nay de
// sai neu tu doan lai tu datasheet ST7789 chung, nen giu nguyen theo board that.
#define BOARD_LCD_SPI_HOST      SPI3_HOST
#define BOARD_LCD_PIN_SCLK      GPIO_NUM_14
#define BOARD_LCD_PIN_MOSI      GPIO_NUM_15
#define BOARD_LCD_PIN_MISO      GPIO_NUM_NC   // ST7789 khong doc du lieu ve, khong can MISO
#define BOARD_LCD_PIN_CS        GPIO_NUM_13
#define BOARD_LCD_PIN_DC        GPIO_NUM_10
#define BOARD_LCD_PIN_RST       GPIO_NUM_16
#define BOARD_LCD_PIN_BACKLIGHT GPIO_NUM_21   // dieu khien qua LEDC PWM (chinh do sang), khong dao muc
#define BOARD_LCD_BACKLIGHT_INVERT false
#define BOARD_LCD_BACKLIGHT_DEFAULT_PERCENT 70

#define BOARD_LCD_H_RES          240
#define BOARD_LCD_V_RES          240
#define BOARD_LCD_OFFSET_X       0
#define BOARD_LCD_OFFSET_Y       0
#define BOARD_LCD_MIRROR_X       false
#define BOARD_LCD_MIRROR_Y       false
#define BOARD_LCD_SWAP_XY        false
#define BOARD_LCD_INVERT_COLOR   true         // bat buoc voi panel nay, khong bat -> mau bi am ban
#define BOARD_LCD_RGB_ELEMENT_ORDER_IS_RGB true // true = RGB, false = BGR
#define BOARD_LCD_SPI_CLOCK_HZ   (40 * 1000 * 1000)

// ---- I2C (dieu khien codec ES8311) -----------------------------------------
#define BOARD_I2C_PORT           I2C_NUM_0
#define BOARD_I2C_PIN_SDA        GPIO_NUM_7
#define BOARD_I2C_PIN_SCL        GPIO_NUM_6
#define BOARD_I2C_FREQ_HZ        100000
// QUAN TRONG: khong phai 0x18 (dia chi 7-bit "sach giao khoa" cua ES8311).
// Component espressif/esp_codec_dev dinh nghia ES8311_CODEC_DEFAULT_ADDR =
// 0x30 (xem managed_components/espressif__esp_codec_dev/device/include/es8311_codec.h)
// va audio_codec_i2c_cfg_t.addr phai khop gia tri nay, neu khong ES8311 se
// khong ACK bat ky giao dich I2C nao (da xac nhan qua log that: "Fail to
// write to dev 18" lap lai lien tuc -> es8311_codec_new that bai -> abort).
#define BOARD_ES8311_I2C_ADDR    0x30

// ---- I2S (du lieu audio den/di ES8311) -------------------------------------
// I2S full-duplex: 1 bo MCLK/BCLK/WS dung chung cho ca ADC (mic) va DAC (loa).
// QUAN TRONG: vi dung chung 1 clock, ca 2 chieu BAT BUOC chay cung 1 sample
// rate phan cung -> chon 16kHz (da kiem chung tren board nay) cho ca thu va
// phat; audio 24kHz Gemini Live tra ve se duoc resample xuong 16kHz truoc khi
// phat (xem audio_pipeline_gemini.c), tranh rui ro chinh sai he so MCLK/LRCK
// khi thu nghiem truc tiep o 24kHz tren phan cung chua kiem chung.
#define BOARD_I2S_PORT            I2S_NUM_0
#define BOARD_I2S_SAMPLE_RATE_HZ  16000
#define BOARD_I2S_PIN_MCLK        GPIO_NUM_5
#define BOARD_I2S_PIN_BCLK        GPIO_NUM_4
#define BOARD_I2S_PIN_WS          GPIO_NUM_2    // LRCLK
#define BOARD_I2S_PIN_DOUT        GPIO_NUM_1    // ESP32 -> ES8311 (tin hieu phat ra loa)
#define BOARD_I2S_PIN_DIN         GPIO_NUM_3    // ES8311 -> ESP32 (tin hieu mic)

#define BOARD_PIN_PA_ENABLE       GPIO_NUM_8    // bat/tat cong suat loa (power amp)

// ---- Nut bam ----------------------------------------------------------------
// Nut noi GND, dung internal pull-up, active level = 0 (nhan xuong).
// BOOT_BUTTON (GPIO0) duoc dung lam nut push-to-talk noi chuyen voi Gemini —
// day cung la chan strapping BOOT cua ESP32-S3, nhung chi anh huong luc
// reset/nap firmware, khong anh huong khi doc GPIO luc runtime nen dung duoc.
#define BOARD_PIN_BTN_TALK        GPIO_NUM_0
#define BOARD_PIN_BTN_VOL_UP      GPIO_NUM_40
#define BOARD_PIN_BTN_VOL_DOWN    GPIO_NUM_39
#define BOARD_BTN_ACTIVE_LEVEL    0
#define BOARD_BTN_DEBOUNCE_MS     30
#define BOARD_BTN_LONG_PRESS_MS   700   // giu vol+/vol- lau -> max/mute

// ---- LED trang thai & pin (co san tren board) -------------------------------
#define BOARD_PIN_STATUS_LED      GPIO_NUM_46   // LED don, bao trang thai (nhap nhay = dang xu ly, sang = loi...)
#define BOARD_PIN_BATTERY_CHARGE  GPIO_NUM_38   // input, muc cao = dang sac
#define BOARD_BATTERY_ADC_UNIT    ADC_UNIT_1
#define BOARD_BATTERY_ADC_CHANNEL ADC_CHANNEL_8 // = GPIO9 tren ESP32-S3
#define BOARD_BATTERY_ADC_ATTEN   ADC_ATTEN_DB_12

// Cong tac nguon: coi la cong tac ngat nguon phan cung thuan tuy (khong noi
// GPIO nao ca, khong can xu ly trong firmware).

// ---- WiFi provisioning (SoftAP) ---------------------------------------------
#define BOARD_PROV_AP_SSID_PREFIX "ESP32-Setup-"
#define BOARD_PROV_AP_PASSWORD    "12345678"   // toi thieu 8 ky tu cho WPA2; doi neu can bao mat hon
#define BOARD_PROV_AP_CHANNEL     1
#define BOARD_PROV_AP_MAX_CONN    4
#define BOARD_PROV_HTTP_PORT      80
#define BOARD_PROV_DNS_PORT       53

// So lan thu ket noi STA truoc khi quay lai che do provisioning
#define BOARD_WIFI_STA_MAX_RETRY  5
