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

/* --- Runtime API ------------------------------------------------------- */
void hud_set_speed(uint16_t kmh);                 /* turns red above the limit */
void hud_set_speed_limit(uint16_t kmh);           /* 0 = unknown -> sign hidden */
void hud_set_warning(uint8_t slot, hud_warn_t w, uint16_t dist_m); /* slot 0..1 */
void hud_clear_warnings(void);

void hud_set_nav(const hud_nav_t *nav);           /* enters navigation mode */
void hud_nav_stop(void);                          /* back to lane-keeping animation */

#ifdef __cplusplus
}
#endif
#endif /* HUD_UI_H */
