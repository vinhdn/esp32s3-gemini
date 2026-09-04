#pragma once

// ============================================================================
// Pinout board "Cheap Yellow Display" (CYD) ESP32-2432S028R - 2.8" ILI9341
// 320x240 SPI + cam ung dien tro XPT2046, ESP32-WROOM-32 (khong PSRAM).
//
// Day la pinout PHO BIEN NHAT cong dong CYD dung (vd du an
// witnessmenow/ESP32-Cheap-Yellow-Display), nhung CAC LO HANG CYD HAN KHAC
// NHAU (dac biet chan backlight va touch IRQ) - neu man hinh khong len hoac
// cam ung sai, doi lai gia tri o day truoc. Day la noi DUY NHAT can sua GPIO
// cho toan bo firmware (cung triet ly voi board_config.h cua board ESP32-S3).
//
// Chan TFT/touch da duoc truyen thang vao build_flags cua platformio.ini
// (TFT_eSPI doc cau hinh qua macro bien dich, khong doc header nay) - SUA CA
// HAI CHO neu doi pin TFT/touch.
// ============================================================================

// ---- LCD ILI9341 (SPI, VSPI mac dinh) --------------------------------------
// Xem them build_flags trong platformio.ini (TFT_MISO/MOSI/SCLK/CS/DC/RST/BL).
#define BOARD_TFT_PIN_MISO   12
#define BOARD_TFT_PIN_MOSI   13
#define BOARD_TFT_PIN_SCLK   14
#define BOARD_TFT_PIN_CS     15
#define BOARD_TFT_PIN_DC     2
#define BOARD_TFT_PIN_RST    -1  // noi truc tiep EN, khong dieu khien rieng
#define BOARD_TFT_PIN_BL     21  // backlight, muc HIGH = sang (TFT_BACKLIGHT_ON)

// Panel goc 240x320 (portrait) - HUD ve o che do ngang (landscape) qua
// tft.setRotation(BOARD_TFT_ROTATION) trong ui.cpp.
// 1 = xoay 90 do (cong CYD ben trai khi nhin tu mat truoc). Doi thanh 3 neu
// hinh bi nguoc/lat trai-phai sau khi flash.
#define BOARD_TFT_ROTATION   1
#define BOARD_LCD_H_RES      320
#define BOARD_LCD_V_RES      240

// ---- Cam ung XPT2046 (dien tro, dung chung bus SPI voi TFT) ----------------
#define BOARD_TOUCH_PIN_CS   33
#define BOARD_TOUCH_PIN_IRQ  36  // input-only pin tren ESP32 classic, dung duoc lam IRQ

// ---- The nho TF/SD (SPI RIENG - chua dung o v1, khai bao san) --------------
#define BOARD_SD_PIN_CS      5
#define BOARD_SD_PIN_MOSI    23
#define BOARD_SD_PIN_MISO    19
#define BOARD_SD_PIN_SCK     18

// ---- LED RGB + cam bien anh sang (chua dung o v1) --------------------------
#define BOARD_LED_PIN_RED    4
#define BOARD_LED_PIN_GREEN  16
#define BOARD_LED_PIN_BLUE   17
#define BOARD_LDR_PIN        34

// ---- BLE --------------------------------------------------------------------
// PHAI giu nguyen giong firmware ESP32-S3 hien tai (esp32/main/ble/
// waze_hud_ble.c) de app Android (com.esp32nav) khong can sua gi - xem
// ImageRelayBle.kt phia app: chi doi ten quang ba + 1 service/1 characteristic
// ghi duoc nay, khong can characteristic notify/capabilities.
#define BOARD_BLE_DEVICE_NAME     "VIETMAP_HUD_H50"
#define BOARD_BLE_SERVICE_UUID    "0000ffff-0000-1000-8000-00805f9b34fb"
#define BOARD_BLE_WRITE_CHAR_UUID "00009abc-0000-1000-8000-00805f9b34fb"
