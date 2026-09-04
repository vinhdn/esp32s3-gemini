#ifndef HUD_LANE_ASSETS_H
#define HUD_LANE_ASSETS_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const lv_image_dsc_t car_top;          /* 40x64 RGB565A8 */
extern const lv_image_dsc_t lane_dash;       /* 2x14 A8 */
extern const lv_image_dsc_t lane_dash_center; /* 1x8 A8 */

/* Geometry used by hud_ui.c build_lane() */
#define HUD_LANE_LEFT_X    37   /* x of the left lane marking  */
#define HUD_LANE_RIGHT_X   132  /* x of the right lane marking */
#define HUD_LANE_CENTER_X  84   /* x of the faint centre line  */
#define HUD_LANE_PERIOD    30   /* dash + gap, side lanes      */
#define HUD_LANE_PERIOD_C  26   /* dash + gap, centre line     */
#define HUD_CAR_Y          84   /* car top offset in the panel */
#define HUD_CAR_SWAY       3    /* +/- px lateral sway         */

#ifdef __cplusplus
}
#endif
#endif /* HUD_LANE_ASSETS_H */
