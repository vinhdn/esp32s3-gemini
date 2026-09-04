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

    /* two upcoming road signs, slot 0 = nearest (pulses) */
    hud_set_sign(0, HUD_SIGN_SPEEDCAM, 400);
    hud_set_sign(1, HUD_SIGN_PEDESTRIAN, 1200);

    /* 5-day forecast, day 0 = today */
    hud_set_forecast(0, "NAY", HUD_WX_SUN,    33);
    hud_set_forecast(1, "T6",  HUD_WX_PARTLY, 31);
    hud_set_forecast(2, "T7",  HUD_WX_RAIN,   28);
    hud_set_forecast(3, "CN",  HUD_WX_STORM,  27);
    hud_set_forecast(4, "T2",  HUD_WX_SUN,    32);

    /* map view is the default; set its captions */
    hud_map_set_street("");        /* feed the real street from your GPS source */
    hud_map_set_scale("200 m");
    hud_map_set_heading(0);        /* rotates the marker in both map views */

    hud_nav_t nav = {
        .turn = HUD_TURN_RIGHT, .dist_m = 300,
        .street = "Nguyen Trai",
        .hint = "Di lan ben phai, sau do di thang 2,1 km",
        .remain_100m = 84, .eta_min = 12, .arrive_hhmm = "14:32",
    };
    hud_set_nav(&nav);
    /* hud_nav_stop();  -> map view */
}

void loop()
{
    lv_timer_handler();
    delay(5);
}
