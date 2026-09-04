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
#define HUD_FONTS_STUB 1

#if HUD_FONTS_STUB
  #define HUD_F_SPEED   &lv_font_montserrat_48
  #define HUD_F_DIST    &lv_font_montserrat_38
  #define HUD_F_LIMIT   &lv_font_montserrat_24
  #define HUD_F_METRIC  &lv_font_montserrat_16
  #define HUD_F_TEXT    &lv_font_montserrat_14
  #define HUD_F_SMALL   &lv_font_montserrat_12
  #define HUD_F_LABEL   &lv_font_montserrat_10
#else
  LV_FONT_DECLARE(hud_num_62)
  LV_FONT_DECLARE(hud_num_38)
  LV_FONT_DECLARE(hud_num_24)
  LV_FONT_DECLARE(hud_num_16)
  LV_FONT_DECLARE(hud_text_13)
  LV_FONT_DECLARE(hud_text_11)
  LV_FONT_DECLARE(hud_label_9)
  #define HUD_F_SPEED   &hud_num_62
  #define HUD_F_DIST    &hud_num_38
  #define HUD_F_LIMIT   &hud_num_24
  #define HUD_F_METRIC  &hud_num_16
  #define HUD_F_TEXT    &hud_text_13
  #define HUD_F_SMALL   &hud_text_11
  #define HUD_F_LABEL   &hud_label_9
#endif

#endif /* HUD_THEME_H */
