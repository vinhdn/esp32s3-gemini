#ifndef HUD_UI_H
#define HUD_UI_H
#include "lvgl.h"
#include "hud_theme.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HUD_TURN_STRAIGHT,      /* di thang */
    HUD_TURN_LEFT,          /* re trai */
    HUD_TURN_RIGHT,         /* re phai */
    HUD_TURN_SLIGHT_LEFT,   /* chech trai */
    HUD_TURN_SLIGHT_RIGHT,  /* chech phai */
    HUD_TURN_SHARP_LEFT,    /* re gap trai */
    HUD_TURN_SHARP_RIGHT,   /* re gap phai */
    HUD_TURN_U_TURN,        /* quay dau */
    HUD_TURN_MERGE,         /* nhap lan */
    HUD_TURN_EXIT_RIGHT,    /* ra loi re phai */
    HUD_TURN_ROUNDABOUT,    /* vong xuyen */
    HUD_TURN_ARRIVE,        /* den dich */
} hud_turn_t;

typedef enum {
    HUD_WARN_NONE,
    HUD_WARN_SPEEDCAM,      /* ban toc do */
    HUD_WARN_PEDESTRIAN,    /* nguoi di bo */
    HUD_WARN_ROUGH_ROAD,    /* duong xau */
    HUD_WARN_SHARP_CURVE,   /* cua gap */
} hud_warn_t;

/* CHINH SUA so voi ban goc: cac truong khoang cach/thoi gian nhan CHUOI DA
 * DINH DANG SAN (khong phai so tho) - vi du BLE thuc te (ImageRelayBle/
 * VietmapAccessibilityService.kt) gui thang chuoi da format boi Google Maps
 * ("300 m", "8,4 km", "12 phut", "1.7 mi"...) theo dung locale/don vi may
 * dien thoai dang dung (co the km/mi, dau phay/cham...) - tu parse lai thanh
 * so nguyen se mat chinh xac hoac loi voi dinh dang khong luong truoc duoc.
 * Hien thi nguyen van chuoi nhan duoc la cach an toan nhat. */
typedef struct {
    hud_turn_t  turn;
    const char *dist;          /* "300 m" - khoang cach toi diem re, DA CO don vi */
    const char *street;        /* "Nguyen Trai" */
    const char *hint;          /* "Di lan ben phai, sau do di thang 2,1 km" */
    const char *remain;        /* "8,4 km" - quang duong con lai, DA CO don vi */
    const char *time_remaining;/* "12 phut" */
    const char *arrive_hhmm;   /* "14:32" */
} hud_nav_t;

/* Build the whole screen once, on the active display. */
void hud_ui_init(void);

/* --- Runtime API ------------------------------------------------------- */
void hud_set_speed(uint16_t kmh);                 /* turns red above the limit */
void hud_set_speed_limit(uint16_t kmh);           /* 0 = unknown -> sign hidden */
void hud_set_warning(uint8_t slot, hud_warn_t w, uint16_t dist_m); /* slot 0..1 */
void hud_clear_warnings(void);

/* Kich thuoc canvas anh icon canh bao THAT (giai ma tu JPEG board S3 gui -
 * xem icon_stream.cpp) - vuong, RGB565, dung dung kich thuoc nay. */
#define HUD_WARNING_ICON_SIZE 64

/* Thay icon tinh (hud_warn_t) bang anh THAT giai ma duoc (RGB565,
 * HUD_WARNING_ICON_SIZE x HUD_WARNING_ICON_SIZE) - COPY vao buffer rieng cua
 * hud_ui, con tro truyen vao chi can hop le trong luc goi ham nay. NULL ->
 * quay lai icon tinh cua hud_set_warning gan nhat. Doc lap voi
 * hud_set_warning() (co the goi bat cu thu tu nao). */
void hud_set_warning_icon_image(uint8_t slot, const uint16_t *rgb565);

void hud_set_nav(const hud_nav_t *nav);           /* enters navigation mode */
void hud_nav_stop(void);                          /* back to lane-keeping animation */

typedef enum {
    HUD_WEATHER_NONE,   /* khong co du lieu - hien "--", an icon */
    HUD_WEATHER_SUNNY,
    HUD_WEATHER_CLOUDY,
    HUD_WEATHER_RAIN,
    HUD_WEATHER_STORM,
    HUD_WEATHER_SNOW,
} hud_weather_t;

/* Du bao thoi tiet hom nay/ngay mai (frame VMSX, gan voi VietMap - xem
 * ble_server.cpp), hien duoi cum "CANH BAO TIEP" (nua trai): nhiet do (so)
 * phia tren, icon dieu kien phia duoi - khong hien chu "Hom nay/Ngay mai".
 * today_cond/tomorrow_cond = HUD_WEATHER_NONE -> hien "--" khong icon (giu
 * bo cuc on dinh, giong sign_limit). */
void hud_set_weather(int8_t today_temp_c, hud_weather_t today_cond,
                      int8_t tomorrow_temp_c, hud_weather_t tomorrow_cond);

#ifdef __cplusplus
}
#endif
#endif /* HUD_UI_H */
