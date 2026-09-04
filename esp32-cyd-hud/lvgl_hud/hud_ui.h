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

/* Upcoming road signs, drawn in their real shape and colours. */
typedef enum {
    HUD_SIGN_NONE,
    HUD_SIGN_SPEEDCAM,       /* tron  - ban toc do      */
    HUD_SIGN_NO_OVERTAKE,    /* tron  - cam vuot        */
    HUD_SIGN_NO_HORN,        /* tron  - cam bam con     */
    HUD_SIGN_PEDESTRIAN,     /* tamgc - nguoi di bo     */
    HUD_SIGN_SHARP_CURVE,    /* tamgc - cua gap         */
    HUD_SIGN_ROUGH_ROAD,     /* tamgc - duong xau       */
    HUD_SIGN_CHILDREN,       /* tamgc - tre em          */
    HUD_SIGN_TRAFFIC_LIGHT,  /* tamgc - den tin hieu    */
} hud_sign_t;

typedef enum {
    HUD_WX_SUN,
    HUD_WX_PARTLY,
    HUD_WX_CLOUD,
    HUD_WX_RAIN,
    HUD_WX_STORM,
    HUD_WX_FOG,
} hud_wx_t;

#define HUD_WX_DAYS  5

typedef struct {
    hud_turn_t  turn;
    uint16_t    dist_m;        /* metres to the manoeuvre */
    const char *street;        /* "Nguyen Trai" */
    const char *hint;          /* "Di lan ben phai, sau do di thang 2,1 km" */
    uint16_t    remain_100m;   /* remaining distance in 100 m units -> 84 = 8,4 km */
    uint16_t    eta_min;       /* minutes left */
    const char *arrive_hhmm;   /* "14:32" */
} hud_nav_t;

/* Build the whole screen once, on the active display. */
void hud_ui_init(void);

/* --- Left column ------------------------------------------------------- */
void hud_set_speed(uint16_t kmh);                 /* turns red above the limit */
void hud_set_speed_limit(uint16_t kmh);           /* 0 = unknown -> sign hidden */

/* Two upcoming signs, slot 0 = nearest (it pulses). dist_m < 1000 shows "m",
 * above that "x,y" + "km". HUD_SIGN_NONE hides the slot. */
void hud_set_sign(uint8_t slot, hud_sign_t sign, uint16_t dist_m);
void hud_clear_signs(void);

/* Five-day forecast strip. day: 0 = today (label is highlighted).
 * label is a 2-3 char weekday ("T6", "CN"); temp_c is the daily high. */
void hud_set_forecast(uint8_t day, const char *label, hud_wx_t cond, int8_t temp_c);

/* --- Right half -------------------------------------------------------- */
void hud_set_nav(const hud_nav_t *nav);           /* navigation view */
void hud_nav_stop(void);                          /* back to the map view */

/* Map view: current street name and the scale-bar caption ("200 m"). */
void hud_map_set_street(const char *street);
void hud_map_set_scale(const char *txt);
/* Heading of the position marker, 0-359 degrees, 0 = north / up. */
void hud_map_set_heading(uint16_t deg);

#ifdef __cplusplus
}
#endif
#endif /* HUD_UI_H */
