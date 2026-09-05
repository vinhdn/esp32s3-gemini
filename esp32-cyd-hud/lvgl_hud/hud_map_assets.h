#ifndef HUD_MAP_ASSETS_H
#define HUD_MAP_ASSETS_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t map_tile;   /* 169x240 RGB565   */
extern const lv_image_dsc_t map_arrow;  /* 20x22   RGB565A8 */
extern const lv_image_dsc_t map_north;  /* 10x12   A8       */

extern const lv_image_dsc_t map_tile_nav; /* 159x136 RGB565 - nav-mode inset */

/* Where the position marker sits on each tile (centre of the arrow). */
#define HUD_MAP_POS_X      60
#define HUD_MAP_POS_Y      178
#define HUD_MAP_NAV_TOP    46   /* y of the nav map inside nav_panel */
#define HUD_MAP_NAV_W      159
#define HUD_MAP_NAV_H      136
#define HUD_MAP_NAV_POS_X  76
#define HUD_MAP_NAV_POS_Y  106

#ifdef __cplusplus
}
#endif
#endif /* HUD_MAP_ASSETS_H */
