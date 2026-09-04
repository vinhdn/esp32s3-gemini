#include "ui.h"

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "hud_state.h"
#include "hud_ui.h"

// ============================================================================
// Display + touch driver (TFT_eSPI lam backend cho LVGL 9.x)
// ============================================================================

static TFT_eSPI s_tft = TFT_eSPI();

#define UI_BUF_LINES 30
static uint8_t s_buf1[BOARD_LCD_H_RES * UI_BUF_LINES * 2]; // RGB565 = 2 byte/px

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors((uint16_t *)px, w * h, true);
    s_tft.endWrite();

    lv_display_flush_ready(disp);
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t tx, ty;
    if (s_tft.getTouch(&tx, &ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static uint32_t tick_cb()
{
    return millis();
}

static void display_driver_init()
{
    s_tft.init();
    s_tft.setRotation(BOARD_TFT_ROTATION);
    s_tft.setSwapBytes(true); // LVGL dua uint16 RGB565 little-endian, TFT_eSPI can big-endian tren SPI
    // Panel CYD nay hien mau AM BAN neu khong dao (den->trang, do->cyan...) -
    // xac nhan qua test that: nen "den" hien ra trang khi chua bat dong nay.
    s_tft.invertDisplay(true);
    s_tft.fillScreen(TFT_BLACK);

    // Hieu chinh cam ung tho: neu cham lech nhieu, thay 5 gia tri nay bang
    // ket qua tu vi du TFT_eSPI/TOUCH_calibrate chay rieng tren board that.
    uint16_t calib[5] = {300, 3600, 300, 3600, 1};
    s_tft.setTouch(calib);

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, s_buf1, NULL, sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);
}

// ============================================================================
// hud_ui.c (thiet ke lvgl_hud/) lam toan bo giao dien - o day chi anh xa
// hud_state_t (BLE/VMSX + nav JSON da parse) vao API cua no.
// ============================================================================

void ui_init()
{
    display_driver_init();
    hud_ui_init();
}

static hud_turn_t direction_to_turn(const char *dir)
{
    if (!dir) return HUD_TURN_STRAIGHT;
    if (strcmp(dir, "turn_left") == 0) return HUD_TURN_LEFT;
    if (strcmp(dir, "turn_right") == 0) return HUD_TURN_RIGHT;
    if (strcmp(dir, "slight_left") == 0) return HUD_TURN_SLIGHT_LEFT;
    if (strcmp(dir, "slight_right") == 0) return HUD_TURN_SLIGHT_RIGHT;
    if (strcmp(dir, "sharp_left") == 0) return HUD_TURN_SHARP_LEFT;
    if (strcmp(dir, "sharp_right") == 0) return HUD_TURN_SHARP_RIGHT;
    if (strcmp(dir, "u_turn") == 0) return HUD_TURN_U_TURN;
    if (strcmp(dir, "merge") == 0) return HUD_TURN_MERGE;
    if (strcmp(dir, "exit_right") == 0) return HUD_TURN_EXIT_RIGHT;
    if (strcmp(dir, "exit_left") == 0) return HUD_TURN_EXIT_RIGHT; // hud_ui khong co exit_left rieng
    if (strcmp(dir, "roundabout") == 0) return HUD_TURN_ROUNDABOUT;
    if (strcmp(dir, "arrive") == 0) return HUD_TURN_ARRIVE;
    return HUD_TURN_STRAIGHT;
}

void ui_refresh()
{
    hud_state_t s;
    hud_state_lock();
    s = g_hud_state;
    hud_state_unlock();

    hud_set_speed(s.current_speed_kmh);
    hud_set_speed_limit(s.speed_limit_kmh);

    // Slot 0 = camera ban toc do (VMSX camera_*) - dung dung HUD_WARN_SPEEDCAM
    // (icon+mau amber urgent, khop thiet ke goc).
    if (s.camera_valid) {
        hud_set_warning(0, HUD_WARN_SPEEDCAM, s.camera_distance_m);
    } else {
        hud_set_warning(0, HUD_WARN_NONE, 0);
    }
    // Slot 1 = bien bao gioi han sap toi (VMSX next_limit_*) - thiet ke goc
    // khong co kieu canh bao "bien bao sap toi" rieng, tam dung icon/kieu
    // HUD_WARN_PEDESTRIAN (trung, khong urgent) cho khu vuc nay giong dung
    // vi tri o mockup, chi khac y nghia icon.
    if (s.next_limit_valid) {
        hud_set_warning(1, HUD_WARN_PEDESTRIAN, s.next_limit_distance_m);
    } else {
        hud_set_warning(1, HUD_WARN_NONE, 0);
    }

    if (s.nav_active) {
        hud_nav_t nav = {};
        nav.turn = direction_to_turn(s.nav_direction);
        nav.dist = s.nav_distance[0] ? s.nav_distance : nullptr;
        nav.street = s.nav_road;
        nav.hint = s.nav_instruction;
        nav.remain = s.nav_total_dist[0] ? s.nav_total_dist : nullptr;
        nav.time_remaining = s.nav_time_remaining[0] ? s.nav_time_remaining : nullptr;
        nav.arrive_hhmm = s.nav_eta[0] ? s.nav_eta : nullptr;
        hud_set_nav(&nav);
    } else {
        hud_nav_stop();
    }
}
