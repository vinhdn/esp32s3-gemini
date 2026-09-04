#include "ui.h"

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "hud_state.h"

LV_FONT_DECLARE(lv_font_vi_14);
LV_FONT_DECLARE(lv_font_vi_16);
LV_FONT_DECLARE(lv_font_vi_20);

// ============================================================================
// Display + touch driver (TFT_eSPI lam backend cho LVGL)
// ============================================================================

static TFT_eSPI s_tft = TFT_eSPI();

#define UI_BUF_LINES 30
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[BOARD_LCD_H_RES * UI_BUF_LINES];

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    s_tft.endWrite();

    lv_disp_flush_ready(drv);
}

static void touchpad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tx, ty;
    if (s_tft.getTouch(&tx, &ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void display_driver_init()
{
    s_tft.init();
    s_tft.setRotation(BOARD_TFT_ROTATION);
    s_tft.setSwapBytes(true); // LVGL dua uint16 RGB565 little-endian, TFT_eSPI can big-endian tren SPI
    // Panel CYD nay hien mau AM BAN neu khong dao (den->trang, do->cyan...) -
    // xac nhan qua test that: nen "den" hien ra trang khi chua bat dong nay.
    s_tft.invertDisplay(true);
    s_tft.fillScreen(TFT_BLACK);

    // Hieu chinh cam ung tho: neu cham lech nhieu, thay 5 gia tri nay bang
    // ket qua tu vi du TFT_eSPI/TOUCH_calibrate chay rieng tren board that.
    uint16_t calib[5] = {300, 3600, 300, 3600, 1};
    s_tft.setTouch(calib);

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, BOARD_LCD_H_RES * UI_BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = BOARD_LCD_H_RES;
    disp_drv.ver_res = BOARD_LCD_V_RES;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);
}

// ============================================================================
// Mau sac - theme toi, phong theo mockup (nen den tuyet doi, tach 2 nua trai/
// phai bang duong ke doc).
// ============================================================================

#define COL_BG        lv_color_black()
#define COL_CARD      lv_color_hex(0x14181D)
#define COL_DIVIDER   lv_color_hex(0x2A2E33)
#define COL_TEXT      lv_color_hex(0xFFFFFF)
#define COL_TEXT_DIM  lv_color_hex(0x8B929A)
#define COL_DANGER    lv_color_hex(0xE53935) // do - vien bien bao / qua toc do
#define COL_CAMERA    lv_color_hex(0xFFA726) // cam - icon camera ban toc do
#define COL_NAV       lv_color_hex(0x29B6F6) // xanh duong - khoi dan duong

#define LEFT_W 152
#define DIVIDER_X 154
#define RIGHT_X 160
#define RIGHT_W 152

// ---- Nua trai: toc do + bien bao + 2 the canh bao ----
static lv_obj_t *s_speed_label;
static lv_obj_t *s_sign_badge;
static lv_obj_t *s_sign_number;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *icon;
    lv_obj_t *value;
    lv_obj_t *caption;
} alert_card_t;

static alert_card_t s_camera_card;
static alert_card_t s_sign_card;
static lv_obj_t *s_sign_card_mini_badge; // badge tron nho (giong s_sign_badge) thay icon cho the "bien bao sap toi"
static lv_obj_t *s_sign_card_mini_number;

// ---- Nua phai: dan duong Google Maps ----
static lv_obj_t *s_nav_icon;
static lv_obj_t *s_nav_distance;
static lv_obj_t *s_nav_turn_text;
static lv_obj_t *s_nav_road;
static lv_obj_t *s_nav_instruction;
static lv_obj_t *s_nav_placeholder;
static lv_obj_t *s_nav_content_group; // an/hien ca cum khi khong co dan duong

typedef struct {
    lv_obj_t *col;
    lv_obj_t *label;
    lv_obj_t *value;
} stat_col_t;

static stat_col_t s_stat_total_dist;
static stat_col_t s_stat_time;

static lv_obj_t *s_conn_led;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// Badge tron (bien bao gioi han): vien do/trang khi co so, xam nhat khi khong.
static lv_obj_t *make_sign_badge(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t d)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, d >= 50 ? 5 : 3, 0);
    lv_obj_set_style_border_color(b, COL_DANGER, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

static alert_card_t make_alert_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    alert_card_t c;
    c.card = lv_obj_create(parent);
    lv_obj_remove_style_all(c.card);
    lv_obj_set_pos(c.card, x, y);
    lv_obj_set_size(c.card, w, h);
    lv_obj_set_style_bg_opa(c.card, LV_OPA_TRANSP, 0); // khong nen, icon/chu noi truc tiep tren nen den
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);

    // LV_SYMBOL_* nam trong vung Unicode rieng (private-use area) khong co
    // trong font tieng Viet tu build (vi_14/16/20) - PHAI dung font Montserrat
    // built-in cua LVGL (luon kem san bo glyph symbol) cho moi label hien icon.
    c.icon = make_label(c.card, &lv_font_montserrat_28, COL_CAMERA);
    lv_obj_align(c.icon, LV_ALIGN_TOP_MID, 0, 10);

    c.value = make_label(c.card, &lv_font_vi_16, COL_TEXT);
    lv_obj_align(c.value, LV_ALIGN_TOP_MID, 0, 42);

    c.caption = make_label(c.card, &lv_font_vi_14, COL_TEXT_DIM);
    lv_label_set_long_mode(c.caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(c.caption, w - 8);
    lv_obj_set_style_text_align(c.caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c.caption, LV_ALIGN_BOTTOM_MID, 0, -8);

    return c;
}

static stat_col_t make_stat_col(lv_obj_t *parent, lv_coord_t x, lv_coord_t w)
{
    stat_col_t s;
    s.col = lv_obj_create(parent);
    lv_obj_remove_style_all(s.col);
    lv_obj_set_pos(s.col, x, 200);
    lv_obj_set_size(s.col, w, 36);
    lv_obj_clear_flag(s.col, LV_OBJ_FLAG_SCROLLABLE);

    s.label = make_label(s.col, &lv_font_vi_14, COL_TEXT_DIM);
    lv_obj_align(s.label, LV_ALIGN_TOP_MID, 0, 0);

    s.value = make_label(s.col, &lv_font_vi_16, COL_TEXT);
    lv_obj_align(s.value, LV_ALIGN_TOP_MID, 0, 16);

    return s;
}

void ui_init()
{
    display_driver_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);

    // ===== NUA TRAI =====
    lv_obj_t *speed_title = make_label(scr, &lv_font_vi_14, COL_TEXT_DIM);
    lv_label_set_text(speed_title, "TỐC ĐỘ");
    lv_obj_set_pos(speed_title, 10, 6);

    s_speed_label = make_label(scr, &lv_font_montserrat_48, COL_TEXT);
    lv_label_set_text(s_speed_label, "0");
    lv_obj_set_pos(s_speed_label, 8, 18);

    lv_obj_t *speed_unit = make_label(scr, &lv_font_vi_14, COL_TEXT_DIM);
    lv_label_set_text(speed_unit, "km/h");
    lv_obj_set_pos(speed_unit, 12, 68);

    s_sign_badge = make_sign_badge(scr, 84, 6, 60);
    s_sign_number = make_label(s_sign_badge, &lv_font_vi_20, lv_color_black());
    lv_obj_center(s_sign_number);
    lv_label_set_text(s_sign_number, "-");

    lv_obj_t *alerts_title = make_label(scr, &lv_font_vi_14, COL_TEXT_DIM);
    lv_label_set_text(alerts_title, "CẢNH BÁO TIẾP");
    lv_obj_set_pos(alerts_title, 10, 91);

    s_camera_card = make_alert_card(scr, 8, 106, 68, 116);
    lv_label_set_text(s_camera_card.icon, LV_SYMBOL_VIDEO);

    s_sign_card = make_alert_card(scr, 78, 106, 68, 116);
    lv_obj_add_flag(s_sign_card.icon, LV_OBJ_FLAG_HIDDEN); // dung mini badge thay icon chu
    s_sign_card_mini_badge = make_sign_badge(s_sign_card.card, 19, 8, 30);
    s_sign_card_mini_number = make_label(s_sign_card_mini_badge, &lv_font_vi_14, lv_color_black());
    lv_obj_center(s_sign_card_mini_number);
    lv_label_set_text(s_sign_card_mini_number, "-");

    // ===== NUA PHAI (dan duong Google Maps) =====
    s_nav_content_group = lv_obj_create(scr);
    lv_obj_remove_style_all(s_nav_content_group);
    lv_obj_set_pos(s_nav_content_group, 0, 0);
    lv_obj_set_size(s_nav_content_group, 320, 240);
    lv_obj_clear_flag(s_nav_content_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_nav_content_group, LV_OBJ_FLAG_CLICKABLE);

    s_nav_icon = make_label(s_nav_content_group, &lv_font_montserrat_28, COL_NAV);
    lv_label_set_text(s_nav_icon, LV_SYMBOL_UP);
    lv_obj_set_pos(s_nav_icon, RIGHT_X, 6);

    s_nav_distance = make_label(s_nav_content_group, &lv_font_vi_20, COL_TEXT);
    lv_obj_set_pos(s_nav_distance, RIGHT_X + 34, 4);

    s_nav_turn_text = make_label(s_nav_content_group, &lv_font_vi_16, COL_NAV);
    lv_obj_set_pos(s_nav_turn_text, RIGHT_X, 40);

    s_nav_road = make_label(s_nav_content_group, &lv_font_vi_20, COL_TEXT);
    // Ten duong co the rat dai (lay nguyen van tu Google Maps) - chay qua lai
    // (marquee) thay vi cat "..." de doc duoc het, giong hanh vi board S3
    // (ui_screens.c dung LV_LABEL_LONG_SCROLL_CIRCULAR cho nav_road_label).
    lv_label_set_long_mode(s_nav_road, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_nav_road, RIGHT_W - 8);
    lv_obj_set_pos(s_nav_road, RIGHT_X, 74);

    s_nav_instruction = make_label(s_nav_content_group, &lv_font_vi_14, COL_TEXT_DIM);
    lv_label_set_long_mode(s_nav_instruction, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_nav_instruction, RIGHT_W - 8);
    lv_obj_set_pos(s_nav_instruction, RIGHT_X, 100);

    // Chi 2 cot (bo "DEN"/ETA - khong du cho ngang cho 3 cot doc duoc, theo
    // yeu cau chinh giao dien).
    lv_coord_t col_w = (RIGHT_W - 8) / 2;
    s_stat_total_dist = make_stat_col(s_nav_content_group, RIGHT_X, col_w);
    s_stat_time = make_stat_col(s_nav_content_group, RIGHT_X + col_w, col_w);
    lv_label_set_text(s_stat_total_dist.label, "CÒN LẠI");
    lv_label_set_text(s_stat_time.label, "THỜI GIAN");

    s_nav_placeholder = make_label(scr, &lv_font_vi_16, COL_TEXT_DIM);
    lv_label_set_long_mode(s_nav_placeholder, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_nav_placeholder, RIGHT_W - 40);
    lv_label_set_text(s_nav_placeholder, "Chưa có dẫn đường");
    lv_obj_align(s_nav_placeholder, LV_ALIGN_CENTER, RIGHT_X / 2 + 20, 0);

    // ---- Den bao ket noi BLE (goc phai tren) ----
    s_conn_led = lv_led_create(scr);
    lv_obj_set_size(s_conn_led, 10, 10);
    lv_obj_align(s_conn_led, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_led_set_color(s_conn_led, lv_palette_main(LV_PALETTE_GREY));
    lv_led_off(s_conn_led);
}

// ============================================================================
// Cap nhat noi dung theo hud_state_t
// ============================================================================

typedef struct {
    const char *key;
    const char *symbol;
    const char *label;
} direction_entry_t;

static const direction_entry_t DIRECTION_TABLE[] = {
    {"turn_left", LV_SYMBOL_LEFT, "RẼ TRÁI"},
    {"turn_right", LV_SYMBOL_RIGHT, "RẼ PHẢI"},
    {"sharp_left", LV_SYMBOL_LEFT, "RẼ GẮT TRÁI"},
    {"sharp_right", LV_SYMBOL_RIGHT, "RẼ GẮT PHẢI"},
    {"slight_left", LV_SYMBOL_LEFT, "CHẾCH TRÁI"},
    {"slight_right", LV_SYMBOL_RIGHT, "CHẾCH PHẢI"},
    {"straight", LV_SYMBOL_UP, "ĐI THẲNG"},
    {"merge", LV_SYMBOL_UP, "NHẬP LÀN"},
    {"u_turn", LV_SYMBOL_REFRESH, "QUAY ĐẦU"},
    {"roundabout", LV_SYMBOL_REFRESH, "VÒNG XUYẾN"},
    {"exit_right", LV_SYMBOL_RIGHT, "RA LỐI PHẢI"},
    {"exit_left", LV_SYMBOL_LEFT, "RA LỐI TRÁI"},
    {"arrive", LV_SYMBOL_HOME, "ĐÃ ĐẾN NƠI"},
};

static void direction_info(const char *dir, const char **symbol, const char **label)
{
    if (dir) {
        for (size_t i = 0; i < sizeof(DIRECTION_TABLE) / sizeof(DIRECTION_TABLE[0]); ++i) {
            if (strcmp(dir, DIRECTION_TABLE[i].key) == 0) {
                *symbol = DIRECTION_TABLE[i].symbol;
                *label = DIRECTION_TABLE[i].label;
                return;
            }
        }
    }
    *symbol = LV_SYMBOL_UP;
    *label = "ĐI THẲNG";
}

static void refresh_sign_badge(const hud_state_t &s)
{
    char buf[8];
    // Vien luon do (giong bien bao giao thong that) bat ke co so hay khong -
    // chi doi chu/mau chu ben trong.
    if (s.speed_limit_kmh > 0) {
        lv_obj_set_style_text_color(s_sign_number, lv_color_black(), 0);
        snprintf(buf, sizeof(buf), "%u", s.speed_limit_kmh);
        lv_label_set_text(s_sign_number, buf);
    } else {
        lv_obj_set_style_text_color(s_sign_number, COL_TEXT_DIM, 0);
        lv_label_set_text(s_sign_number, "-");
    }
}

static void refresh_camera_card(const hud_state_t &s)
{
    char buf[16];
    if (s.camera_valid) {
        lv_obj_set_style_text_color(s_camera_card.icon, COL_CAMERA, 0);
        lv_obj_set_style_text_color(s_camera_card.value, COL_TEXT, 0);
        snprintf(buf, sizeof(buf), "%u m", s.camera_distance_m);
        lv_label_set_text(s_camera_card.value, buf);
        lv_label_set_text(s_camera_card.caption, "BẮN TỐC ĐỘ");
    } else {
        lv_obj_set_style_text_color(s_camera_card.icon, COL_TEXT_DIM, 0);
        lv_obj_set_style_text_color(s_camera_card.value, COL_TEXT_DIM, 0);
        lv_label_set_text(s_camera_card.value, "--");
        lv_label_set_text(s_camera_card.caption, "");
    }
}

static void refresh_sign_card(const hud_state_t &s)
{
    char buf[16];
    // Vien luon do (giong s_sign_badge) bat ke co du lieu hay khong.
    if (s.next_limit_valid) {
        lv_obj_set_style_text_color(s_sign_card_mini_number, lv_color_black(), 0);
        if (s.next_limit_kmh > 0) {
            snprintf(buf, sizeof(buf), "%u", s.next_limit_kmh);
        } else {
            snprintf(buf, sizeof(buf), "?");
        }
        lv_label_set_text(s_sign_card_mini_number, buf);

        lv_obj_set_style_text_color(s_sign_card.value, COL_TEXT, 0);
        snprintf(buf, sizeof(buf), "%u m", s.next_limit_distance_m);
        lv_label_set_text(s_sign_card.value, buf);
        lv_label_set_text(s_sign_card.caption, "BIỂN BÁO SẮP TỚI");
    } else {
        lv_obj_set_style_text_color(s_sign_card_mini_number, COL_TEXT_DIM, 0);
        lv_label_set_text(s_sign_card_mini_number, "-");

        lv_obj_set_style_text_color(s_sign_card.value, COL_TEXT_DIM, 0);
        lv_label_set_text(s_sign_card.value, "--");
        lv_label_set_text(s_sign_card.caption, "");
    }
}

static void refresh_nav(const hud_state_t &s)
{
    if (!s.nav_active) {
        lv_obj_add_flag(s_nav_content_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_nav_placeholder, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_nav_content_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_nav_placeholder, LV_OBJ_FLAG_HIDDEN);

    const char *symbol;
    const char *label;
    direction_info(s.nav_direction, &symbol, &label);
    lv_label_set_text(s_nav_icon, symbol);
    lv_label_set_text(s_nav_turn_text, label);
    lv_label_set_text(s_nav_distance, s.nav_distance[0] ? s.nav_distance : "--");
    lv_label_set_text(s_nav_road, s.nav_road);
    lv_label_set_text(s_nav_instruction, s.nav_instruction);
    lv_label_set_text(s_stat_total_dist.value, s.nav_total_dist[0] ? s.nav_total_dist : "--");
    lv_label_set_text(s_stat_time.value, s.nav_time_remaining[0] ? s.nav_time_remaining : "--");
}

void ui_refresh()
{
    hud_state_t snapshot;
    hud_state_lock();
    snapshot = g_hud_state;
    hud_state_unlock();

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", snapshot.current_speed_kmh);
    lv_label_set_text(s_speed_label, buf);
    lv_obj_set_style_text_color(s_speed_label, snapshot.over_speed ? COL_DANGER : COL_TEXT, 0);

    refresh_sign_badge(snapshot);
    refresh_camera_card(snapshot);
    refresh_sign_card(snapshot);
    refresh_nav(snapshot);

    if (snapshot.connected) {
        lv_led_set_color(s_conn_led, lv_palette_main(LV_PALETTE_GREEN));
        lv_led_on(s_conn_led);
    } else {
        lv_led_set_color(s_conn_led, lv_palette_main(LV_PALETTE_GREY));
        lv_led_off(s_conn_led);
    }
}
