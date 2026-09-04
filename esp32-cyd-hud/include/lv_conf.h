/* Cau hinh LVGL 9.5.0 rut gon cho HUD CYD - chi bat nhung gi hud_ui.c
 * (lvgl_hud/, thiet ke dan xuat tu HUD 320x240.dc.html) dung: lv_obj, lv_label,
 * lv_image + lv_anim core (khong dung widget nao khac). Board khong PSRAM. */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define LV_MEM_SIZE (64U * 1024U) /* 64KB - du cho cac label/anim cap nhat thuong xuyen */
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
    #undef LV_MEM_POOL_INCLUDE
    #undef LV_MEM_POOL_ALLOC
#endif

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  33 /* ~30fps */
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/
/* Khong dung LVGL's OS integration - tu quan ly 1 task FreeRTOS rieng goi
 * lv_timer_handler() dinh ky (xem src/main.cpp). */
#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_DRAW_BUF_STRIDE_ALIGN                1
#define LV_DRAW_BUF_ALIGN                       4
#define LV_DRAW_TRANSFORM_USE_MATRIX            0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (8 * 1024)
#define LV_DRAW_LAYER_MAX_MEMORY 0
#define LV_DRAW_THREAD_STACK_SIZE    (8 * 1024)
#define LV_DRAW_THREAD_PRIO LV_THREAD_PRIO_HIGH

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
    #define LV_DRAW_SW_SUPPORT_RGB565A8     1 /* car_top (hud_lane_assets) */
    #define LV_DRAW_SW_SUPPORT_RGB888       0
    #define LV_DRAW_SW_SUPPORT_XRGB8888     0
    #define LV_DRAW_SW_SUPPORT_ARGB8888     0
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
    #define LV_DRAW_SW_SUPPORT_L8           0
    #define LV_DRAW_SW_SUPPORT_AL88         0
    #define LV_DRAW_SW_SUPPORT_A8           1 /* icon/dash A8 (hud_icons, hud_lane_assets) */
    #define LV_DRAW_SW_SUPPORT_I1           0

    #define LV_DRAW_SW_I1_LUM_THRESHOLD 127
    #define LV_DRAW_SW_DRAW_UNIT_CNT    1
    #define LV_USE_DRAW_ARM2D_SYNC      0
    #define LV_USE_NATIVE_HELIUM_ASM    0
    #define LV_DRAW_SW_COMPLEX          1
    #if LV_DRAW_SW_COMPLEX == 1
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #endif
    #define LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0
#endif

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_USE_REFR_DEBUG 0
#define LV_USE_LAYER_DEBUG 0
#define LV_USE_PARALLEL_DRAW_DEBUG 0
#define LV_USE_OBJ_ID           0
#define LV_USE_OBJ_NAME         0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_GESTURE_RECOGNITION 0
#define LV_USE_FLOAT            0
#define LV_USE_MATRIX           0

/*==================
 * FONT
 *==================*/
#define LV_FONT_MONTSERRAT_14 1 /* du lam LV_FONT_DEFAULT - khong dung de ve chu tieng Viet */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Font tu build (hud_num, hud_text, hud_label - xem src/fonts/) xuat o
 * dang NEN (RLE) - BAT BUOC bat co nay, thieu se render RONG HOAN TOAN
 * (khong phai o vuong) du build khong loi gi - da xac nhan qua test that
 * tren board nay o vong LVGL8 truoc do. */
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_PLACEHOLDER 1

#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 * TEXT SETTINGS
 *==================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/*==================
 * WIDGETS - chi bat nhung gi hud_ui.c dung (lv_obj/lv_label/lv_image)
 *==================*/
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_ARCLABEL   0
#define LV_USE_BAR        0
#define LV_USE_BUTTON        0
#define LV_USE_BUTTONMATRIX  0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     1 /* hien anh icon canh bao that (JPEG giai ma tu board S3 cu) */
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 0
    #define LV_LABEL_LONG_TXT_HINT 0
    #define LV_LABEL_WAIT_CHAR_COUNT 3
#endif
#define LV_USE_LED        0
#define LV_USE_LINE       0
#define LV_USE_LIST       0
#define LV_USE_LOTTIE     0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_3DTEXTURE  0

/*==================
 * THEME - hud_ui.c tu ve toan bo (remove_style_all + mau rieng), khong can theme
 *==================*/
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_USE_FLEX 0
#define LV_USE_GRID 0

/*==================
 * OTHERS - tat het (khong dung filesystem/image-decoder ngoai/observer/...)
 *==================*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0
#define LV_USE_FS_UEFI 0
#define LV_USE_FS_FROGFS 0
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LIBWEBP 0
#define LV_USE_GIF 0
#define LV_USE_GSTREAMER 0
#define LV_USE_RLE 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_GLTF  0
#define LV_USE_VECTOR_GRAPHIC  0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_THORVG_EXTERNAL 0
#define LV_USE_NANOVG 0
#define LV_USE_LZ4_INTERNAL  0
#define LV_USE_LZ4_EXTERNAL  0
#define LV_USE_SVG 0
#define LV_USE_SVG_ANIMATION 0
#define LV_USE_SVG_DEBUG 0
#define LV_USE_FFMPEG 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON   0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_OBSERVER 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0
#define LV_USE_FONT_MANAGER 0
#define LV_USE_TEST 0
#define LV_USE_TEST_SCREENSHOT_COMPARE 0
#define LV_USE_TRANSLATION 0
#define LV_USE_COLOR_FILTER     0
#define LV_USE_NEMA_GFX 0
#define LV_USE_PXP 0
#define LV_USE_G2D 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_SDL 0
#define LV_USE_DRAW_VG_LITE 0
#define LV_USE_DRAW_DMA2D 0
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_PPA  0
#define LV_USE_DRAW_EVE 0
#define LV_USE_DRAW_NANOVG 0
#define LV_USE_SDL              0
#define LV_USE_X11              0
#define LV_USE_WAYLAND          0
#define LV_USE_LINUX_FBDEV      0
#define LV_USE_NUTTX    0
#define LV_USE_LINUX_DRM        0
#define LV_USE_TFT_ESPI         0
#define LV_USE_LOVYAN_GFX         0
#define LV_USE_EVDEV    0
#define LV_USE_LIBINPUT    0
#define LV_USE_ST7735        0
#define LV_USE_ST7789        0
#define LV_USE_ST7796        0
#define LV_USE_ILI9341       0
#define LV_USE_FT81X         0
#define LV_USE_NV3007        0
#define LV_USE_RENESAS_GLCDC    0
#define LV_USE_ST_LTDC    0
#define LV_USE_NXP_ELCDIF   0
#define LV_USE_WINDOWS    0
#define LV_USE_UEFI 0
#define LV_USE_OPENGLES   0
#define LV_USE_GLFW   0
#define LV_USE_QNX              0
#define LV_USE_EXT_DATA   0

/*==================
 * EXAMPLES/DEMOS - khong dung
 *==================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */
