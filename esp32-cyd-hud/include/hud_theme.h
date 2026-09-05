#ifndef HUD_THEME_H
#define HUD_THEME_H
#include "lvgl.h"

/* ---- Palette (matches the HUD mockup; black background for HUD glass) ---- */
#define HUD_C_BG          lv_color_hex(0x000000)
#define HUD_C_PANEL       lv_color_hex(0x0C0F13)
#define HUD_C_LINE        lv_color_hex(0x191F26)
#define HUD_C_BORDER      lv_color_hex(0x2A2F38)
#define HUD_C_TEXT        lv_color_hex(0xE7ECF2)
#define HUD_C_TEXT_DIM    lv_color_hex(0x8B98A6)
#define HUD_C_LABEL       lv_color_hex(0x6B7684)
#define HUD_C_WHITE       lv_color_hex(0xFFFFFF)
#define HUD_C_CYAN        lv_color_hex(0x22D3EE)
#define HUD_C_CYAN_DARK   lv_color_hex(0x0E7F92)
#define HUD_C_AMBER       lv_color_hex(0xFFB020)
#define HUD_C_AMBER_BG    lv_color_hex(0x140F07)
#define HUD_C_AMBER_LINE  lv_color_hex(0x3A2A10)
#define HUD_C_RED         lv_color_hex(0xE02F3C)
#define HUD_C_RED_TEXT    lv_color_hex(0xFF4D4D)
#define HUD_C_LANE        lv_color_hex(0x39424E)

/* ---- Geometry (320 x 240 landscape) ---- */
#define HUD_SCR_W   320
#define HUD_SCR_H   240
#define HUD_LEFT_W  150
#define HUD_RIGHT_W 169   /* 320 - 150 - 1px divider */

/* ---- Fonts -------------------------------------------------------------
 * Generate with lv_font_conv (Barlow Condensed for numbers, Chakra Petch for
 * text). Required Vietnamese subset is listed in README.md.
 * If you have not generated them yet, HUD_FONTS_STUB falls back to Montserrat
 * so the project still compiles (Vietnamese diacritics will be missing).
 * ---------------------------------------------------------------------- */
#define HUD_FONTS_STUB 0

#if HUD_FONTS_STUB
  #define HUD_F_SPEED    &lv_font_montserrat_48
  #define HUD_F_SPEED_SM &lv_font_montserrat_48
  #define HUD_F_DIST     &lv_font_montserrat_38
  #define HUD_F_LIMIT    &lv_font_montserrat_24
  #define HUD_F_LIMIT_LG &lv_font_montserrat_48
  #define HUD_F_METRIC   &lv_font_montserrat_16
  #define HUD_F_TEXT     &lv_font_montserrat_20
  #define HUD_F_SMALL    &lv_font_montserrat_12
  #define HUD_F_LABEL    &lv_font_montserrat_10
#else
  LV_FONT_DECLARE(hud_num_62)
  LV_FONT_DECLARE(hud_num_50)
  LV_FONT_DECLARE(hud_num_39)
  LV_FONT_DECLARE(hud_num_38)
  LV_FONT_DECLARE(hud_num_24)
  LV_FONT_DECLARE(hud_num_16)
  LV_FONT_DECLARE(hud_text_20)
  LV_FONT_DECLARE(hud_text_13)
  LV_FONT_DECLARE(hud_text_11)
  LV_FONT_DECLARE(hud_label_9)
  #define HUD_F_SPEED    &hud_num_62
  /* Speed-limit sign enlarged 1.7x (44->75px) per lvgl_hud design - the
   * current-speed number shrinks to make room (HUD_F_SPEED_SM, 50px,
   * km/h wraps to its own line below) and becomes the 2nd-largest element
   * on the left after the sign itself. */
  #define HUD_F_SPEED_SM &hud_num_50
  #define HUD_F_DIST     &hud_num_38
  #define HUD_F_LIMIT    &hud_num_24
  #define HUD_F_LIMIT_LG &hud_num_39
  /* Da tung tang gap doi (hud_num_32) nhung bi DE/CHONG chu voi nhau tren
   * board that (2 gia tri dai nhu "401 km"+"7h:24p" dinh lien khong cach -
   * xac nhan qua anh chup that) - REVERT ve hud_num_16 (kich thuoc goc)
   * theo yeu cau, cong them scroll ngang khi tran (xem metric()). */
  #define HUD_F_METRIC   &hud_num_16
  /* Da tung tang x3 (hud_text_39) nhung qua to nen chi hien duoc vai ky tu
   * ("towar...", xac nhan qua anh chup that) - giam con hud_text_20 (~nua
   * hud_text_39) theo yeu cau, cho phep xuong dong nhieu dong thay vi
   * "..." 1 dong (xem build_nav()). */
  #define HUD_F_TEXT     &hud_text_20
  #define HUD_F_SMALL    &hud_text_11
  #define HUD_F_LABEL    &hud_label_9
#endif

#endif /* HUD_THEME_H */
