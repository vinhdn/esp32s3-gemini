#ifndef HUD_SIGNS_H
#define HUD_SIGNS_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

/* road signs, 34x34 RGB565A8 - drawn in their real colours */
extern const lv_image_dsc_t sign_speedcam;
extern const lv_image_dsc_t sign_no_overtake;
extern const lv_image_dsc_t sign_no_horn;
extern const lv_image_dsc_t sign_pedestrian;
extern const lv_image_dsc_t sign_sharp_curve;
extern const lv_image_dsc_t sign_rough_road;
extern const lv_image_dsc_t sign_children;
extern const lv_image_dsc_t sign_traffic_light;

/* weather glyphs, 17x17 A8 - recolourable */
extern const lv_image_dsc_t wx_sun;
extern const lv_image_dsc_t wx_partly;
extern const lv_image_dsc_t wx_cloud;
extern const lv_image_dsc_t wx_rain;
extern const lv_image_dsc_t wx_storm;
extern const lv_image_dsc_t wx_fog;

#ifdef __cplusplus
}
#endif
#endif /* HUD_SIGNS_H */
