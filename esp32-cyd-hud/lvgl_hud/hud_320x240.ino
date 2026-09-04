/* hud_320x240.ino - ESP32 + Arduino + LVGL 9.x, 320x240 landscape HUD.
 * Display driver here is TFT_eSPI (ST7789/ILI9341); swap it for your panel.
 */
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "hud_ui.h"

static TFT_eSPI tft;

#define BUF_LINES 40
static uint8_t buf1[HUD_SCR_W * BUF_LINES * 2];   /* RGB565 -> 2 bytes/px */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return millis(); }

void setup()
{
    tft.begin();
    tft.setRotation(1);            /* landscape 320x240 */
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(HUD_SCR_W, HUD_SCR_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    hud_ui_init();

    /* demo state - replace with your GPS / OBD feed */
    hud_set_speed_limit(60);
    hud_set_speed(62);
    hud_set_warning(0, HUD_WARN_SPEEDCAM, 400);
    hud_set_warning(1, HUD_WARN_PEDESTRIAN, 1200);

    hud_nav_t nav = {
        .turn = HUD_TURN_RIGHT, .dist_m = 300,
        .street = "Nguyen Trai",
        .hint = "Di lan ben phai, sau do di thang 2,1 km",
        .remain_100m = 84, .eta_min = 12, .arrive_hhmm = "14:32",
    };
    hud_set_nav(&nav);
    /* hud_nav_stop();  -> lane-keeping animation */
}

void loop()
{
    lv_timer_handler();
    delay(5);
}
