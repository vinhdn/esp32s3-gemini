/* hud_ui.c - 320x240 HUD: speed, speed limit, 2 upcoming warnings,
 * navigation half / lane-keeping animation. LVGL 9.x, no CSS, no scrolling.
 */
#include "hud_ui.h"
#include "hud_icons.h"
#include "hud_lane_assets.h"
#include <stdio.h>
#include <string.h>

/* ---------- widget handles ---------- */
static lv_obj_t *scr;
static lv_obj_t *lbl_speed, *sign_limit, *lbl_limit;
static lv_obj_t *warn_box[2], *warn_icon[2], *warn_dist[2], *warn_name[2];

static lv_obj_t *nav_panel, *nav_icon, *nav_dist, *nav_turn, *nav_street, *nav_hint;
static lv_obj_t *m_remain, *m_eta, *m_arrive;

static lv_obj_t *lane_panel, *lane_track[3], *car;
static lv_anim_t lane_anim, car_anim;

static uint16_t cur_speed = 0, cur_limit = 0;

/* ---------- small helpers ---------- */
static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

/* uppercase micro-label with tracking, as in the mockup */
static lv_obj_t *caption(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = label(parent, txt, HUD_F_LABEL, HUD_C_LABEL);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static void icon_tint(lv_obj_t *img, lv_color_t c)
{
    lv_obj_set_style_image_recolor(img, c, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
}

/* ---------- left column ---------- */
static void build_left(void)
{
    lv_obj_t *col = plain(scr);
    lv_obj_set_size(col, HUD_LEFT_W, HUD_SCR_H);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_pad_all(col, 9, 0);
    lv_obj_set_style_pad_left(col, 10, 0);

    caption(col, "TỐC ĐỘ");
    lv_obj_align(lv_obj_get_child(col, 0), LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_speed = label(col, "0", HUD_F_SPEED, HUD_C_WHITE);
    lv_obj_align(lbl_speed, LV_ALIGN_TOP_LEFT, -2, 12);

    lv_obj_t *unit = label(col, "km/h", HUD_F_SMALL, HUD_C_TEXT_DIM);
    lv_obj_align_to(unit, lbl_speed, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -6);

    /* speed-limit sign: white disc, 5 px red ring */
    sign_limit = plain(col);
    lv_obj_set_size(sign_limit, 46, 46);
    lv_obj_align(sign_limit, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(sign_limit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sign_limit, HUD_C_WHITE, 0);
    lv_obj_set_style_bg_opa(sign_limit, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sign_limit, HUD_C_RED, 0);
    lv_obj_set_style_border_width(sign_limit, 5, 0);
    lbl_limit = label(sign_limit, "--", HUD_F_LIMIT, HUD_C_BG);
    lv_obj_center(lbl_limit);

    lv_obj_t *hr = plain(col);
    lv_obj_set_size(hr, HUD_LEFT_W - 19, 1);
    lv_obj_align(hr, LV_ALIGN_TOP_LEFT, 0, 94);
    lv_obj_set_style_bg_color(hr, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);

    lv_obj_t *cap = caption(col, "CẢNH BÁO TIẾP");
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 100);

    for (int i = 0; i < 2; i++) {
        warn_box[i] = plain(col);
        lv_obj_set_size(warn_box[i], 63, 70);
        lv_obj_align(warn_box[i], LV_ALIGN_TOP_LEFT, i * 69, 118);
        lv_obj_set_style_radius(warn_box[i], 4, 0);
        lv_obj_set_style_bg_color(warn_box[i], HUD_C_PANEL, 0);
        lv_obj_set_style_bg_opa(warn_box[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(warn_box[i], HUD_C_BORDER, 0);
        lv_obj_set_style_border_width(warn_box[i], 1, 0);

        warn_icon[i] = lv_image_create(warn_box[i]);
        lv_image_set_src(warn_icon[i], &icon_speedcam);
        lv_obj_align(warn_icon[i], LV_ALIGN_TOP_MID, 0, 6);
        icon_tint(warn_icon[i], HUD_C_TEXT);

        warn_dist[i] = label(warn_box[i], "--", HUD_F_SMALL, HUD_C_TEXT);
        lv_obj_align(warn_dist[i], LV_ALIGN_TOP_MID, 0, 34);

        warn_name[i] = label(warn_box[i], "", HUD_F_LABEL, HUD_C_LABEL);
        lv_obj_set_style_text_letter_space(warn_name[i], 1, 0);
        lv_obj_align(warn_name[i], LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_add_flag(warn_box[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- navigation half ---------- */
static lv_obj_t *metric(lv_obj_t *parent, const char *cap_txt, int x, lv_align_t al, lv_color_t vc, lv_obj_t **out_val)
{
    lv_obj_t *g = plain(parent);
    lv_obj_set_size(g, 56, 28);
    lv_obj_align(g, al, x, 0);
    lv_obj_t *c = caption(g, cap_txt);
    lv_obj_align(c, al == LV_ALIGN_BOTTOM_RIGHT ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    *out_val = label(g, "--", HUD_F_METRIC, vc);
    lv_obj_align(*out_val, al == LV_ALIGN_BOTTOM_RIGHT ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return g;
}

static void build_nav(void)
{
    lv_obj_t *divider = plain(scr);
    lv_obj_set_size(divider, 1, HUD_SCR_H);
    lv_obj_set_pos(divider, HUD_LEFT_W, 0);
    lv_obj_set_style_bg_color(divider, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    nav_panel = plain(scr);
    lv_obj_set_size(nav_panel, HUD_RIGHT_W, HUD_SCR_H);
    lv_obj_set_pos(nav_panel, HUD_LEFT_W + 1, 0);
    lv_obj_set_style_pad_all(nav_panel, 9, 0);

    nav_icon = lv_image_create(nav_panel);
    lv_image_set_src(nav_icon, &icon_turn_right);
    lv_obj_align(nav_icon, LV_ALIGN_TOP_LEFT, 0, 2);
    icon_tint(nav_icon, HUD_C_CYAN);

    nav_dist = label(nav_panel, "0 m", HUD_F_DIST, HUD_C_WHITE);
    lv_obj_align(nav_dist, LV_ALIGN_TOP_LEFT, 60, 4);

    nav_turn = label(nav_panel, "", HUD_F_LABEL, HUD_C_CYAN);
    lv_obj_set_style_text_letter_space(nav_turn, 1, 0);
    lv_obj_align(nav_turn, LV_ALIGN_TOP_LEFT, 60, 42);

    nav_street = label(nav_panel, "", HUD_F_TEXT, HUD_C_TEXT);
    lv_obj_set_width(nav_street, HUD_RIGHT_W - 20);
    lv_label_set_long_mode(nav_street, LV_LABEL_LONG_DOT);
    lv_obj_align(nav_street, LV_ALIGN_TOP_LEFT, 0, 66);

    nav_hint = label(nav_panel, "", HUD_F_SMALL, HUD_C_TEXT_DIM);
    lv_obj_set_width(nav_hint, HUD_RIGHT_W - 20);
    lv_label_set_long_mode(nav_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(nav_hint, LV_ALIGN_TOP_LEFT, 0, 86);

    lv_obj_t *hr = plain(nav_panel);
    lv_obj_set_size(hr, HUD_RIGHT_W - 20, 1);
    lv_obj_align(hr, LV_ALIGN_BOTTOM_LEFT, 0, -34);
    lv_obj_set_style_bg_color(hr, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);

    metric(nav_panel, "CÒN LẠI", 0,  LV_ALIGN_BOTTOM_LEFT,  HUD_C_TEXT, &m_remain);
    metric(nav_panel, "T.GIAN",  56, LV_ALIGN_BOTTOM_LEFT,  HUD_C_TEXT, &m_eta); // giu viet tat - cot rong 56px, README goc cung dung "T.GIAN"
    metric(nav_panel, "ĐẾN",     0,  LV_ALIGN_BOTTOM_RIGHT, HUD_C_CYAN, &m_arrive);

    lv_obj_add_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- lane-keeping animation (no navigation) ---------- */
static void lane_exec_cb(void *var, int32_t v)
{
    for (int i = 0; i < 3; i++) lv_obj_set_y(lane_track[i], v);
    LV_UNUSED(var);
}

static void car_exec_cb(void *var, int32_t v)
{
    /* v: 0..200 -> lateral sway of +/- 3 px inside the lane */
    int32_t off = (v <= 100) ? (v - 50) : (150 - v);
    lv_obj_align((lv_obj_t *)var, LV_ALIGN_TOP_MID, off * HUD_CAR_SWAY / 50, HUD_CAR_Y);
}

static void build_lane(void)
{
    lane_panel = plain(scr);
    lv_obj_set_size(lane_panel, HUD_RIGHT_W, HUD_SCR_H);
    lv_obj_set_pos(lane_panel, HUD_LEFT_W + 1, 0);
    // LV_OBJ_FLAG_CLIP_CORNER khong con trong LVGL9 (chi con dang style) -
    // lv_obj_set_style_clip_corner() ben duoi la du.
    lv_obj_set_style_clip_corner(lane_panel, true, 0);

    /* three vertical tracks built from the A8 dash sprites; each track is
     * 2x the panel height and scrolls by exactly one dash period. */
    const int lane_x[3]  = { HUD_LANE_LEFT_X, HUD_LANE_RIGHT_X, HUD_LANE_CENTER_X };
    const lv_image_dsc_t *lane_src[3] = { &lane_dash, &lane_dash, &lane_dash_center };
    const lv_color_t lane_col[3] = { HUD_C_LANE, HUD_C_LANE, HUD_C_LINE };
    const int period[3]  = { HUD_LANE_PERIOD, HUD_LANE_PERIOD, HUD_LANE_PERIOD_C };

    for (int i = 0; i < 3; i++) {
        lane_track[i] = plain(lane_panel);
        lv_obj_set_size(lane_track[i], lane_src[i]->header.w, HUD_SCR_H * 2);
        lv_obj_set_pos(lane_track[i], lane_x[i], 0);
        for (int y = 0; y < HUD_SCR_H * 2; y += period[i]) {
            lv_obj_t *d = lv_image_create(lane_track[i]);
            lv_image_set_src(d, lane_src[i]);
            lv_obj_set_pos(d, 0, y);
            icon_tint(d, lane_col[i]);
        }
    }

    /* the car sprite (RGB565A8, colour baked in - no recolour) */
    car = lv_image_create(lane_panel);
    lv_image_set_src(car, &car_top);
    lv_obj_align(car, LV_ALIGN_TOP_MID, 0, HUD_CAR_Y);

    lv_obj_t *foot = caption(lane_panel, "GIỮ LÀN");
    lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_obj_t *foot2 = caption(lane_panel, "KHÔNG DẪN ĐƯỜNG");
    lv_obj_align(foot2, LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    /* dash scroll: one period upward, looped */
    lv_anim_init(&lane_anim);
    lv_anim_set_var(&lane_anim, lane_track[0]);
    lv_anim_set_exec_cb(&lane_anim, lane_exec_cb);
    lv_anim_set_values(&lane_anim, -HUD_LANE_PERIOD, 0);
    lv_anim_set_duration(&lane_anim, 1100);
    lv_anim_set_repeat_count(&lane_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&lane_anim);

    lv_anim_init(&car_anim);
    lv_anim_set_var(&car_anim, car);
    lv_anim_set_exec_cb(&car_anim, car_exec_cb);
    lv_anim_set_values(&car_anim, 0, 200);
    lv_anim_set_duration(&car_anim, 3400);
    lv_anim_set_path_cb(&car_anim, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&car_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&car_anim);
}

/* ---------- public API ---------- */
void hud_ui_init(void)
{
    scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, HUD_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_left();
    build_nav();
    build_lane();
    hud_nav_stop();
}

void hud_set_speed(uint16_t kmh)
{
    char buf[8];
    cur_speed = kmh;
    snprintf(buf, sizeof(buf), "%u", (unsigned)kmh);
    lv_label_set_text(lbl_speed, buf);
    bool over = (cur_limit > 0) && (kmh > cur_limit);
    lv_obj_set_style_text_color(lbl_speed, over ? HUD_C_RED_TEXT : HUD_C_WHITE, 0);
}

void hud_set_speed_limit(uint16_t kmh)
{
    char buf[8];
    cur_limit = kmh;
    if (kmh == 0) { lv_obj_add_flag(sign_limit, LV_OBJ_FLAG_HIDDEN); }
    else {
        lv_obj_clear_flag(sign_limit, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%u", (unsigned)kmh);
        lv_label_set_text(lbl_limit, buf);
    }
    hud_set_speed(cur_speed);
}

void hud_set_warning(uint8_t slot, hud_warn_t w, uint16_t dist_m)
{
    if (slot > 1) return;
    if (w == HUD_WARN_NONE) { lv_obj_add_flag(warn_box[slot], LV_OBJ_FLAG_HIDDEN); return; }

    const lv_image_dsc_t *src = &icon_roughroad;
    const char *name = "";
    bool urgent = false;
    switch (w) {
        case HUD_WARN_SPEEDCAM:    src = &icon_speedcam;   name = "BẮN TỐC ĐỘ";  urgent = true; break;
        case HUD_WARN_PEDESTRIAN:  src = &icon_pedestrian; name = "NGƯỜI ĐI BỘ"; break;
        case HUD_WARN_ROUGH_ROAD:  src = &icon_roughroad;  name = "ĐƯỜNG XẤU";   break;
        case HUD_WARN_SHARP_CURVE: src = &icon_sharpcurve; name = "CUA GẤP";     break;
        default: break;
    }

    lv_obj_clear_flag(warn_box[slot], LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(warn_icon[slot], src);
    icon_tint(warn_icon[slot], urgent ? HUD_C_AMBER : HUD_C_TEXT);
    lv_obj_set_style_bg_color(warn_box[slot], urgent ? HUD_C_AMBER_BG : HUD_C_PANEL, 0);
    lv_obj_set_style_border_color(warn_box[slot], urgent ? HUD_C_AMBER_LINE : HUD_C_BORDER, 0);
    lv_obj_set_style_text_color(warn_dist[slot], urgent ? HUD_C_AMBER : HUD_C_TEXT, 0);
    lv_label_set_text(warn_name[slot], name);

    char buf[12];
    if (dist_m >= 1000) snprintf(buf, sizeof(buf), "%u.%u km", dist_m / 1000, (dist_m % 1000) / 100);
    else                snprintf(buf, sizeof(buf), "%u m", (unsigned)dist_m);
    lv_label_set_text(warn_dist[slot], buf);
}

void hud_clear_warnings(void)
{
    hud_set_warning(0, HUD_WARN_NONE, 0);
    hud_set_warning(1, HUD_WARN_NONE, 0);
}

void hud_set_nav(const hud_nav_t *nav)
{
    if (!nav) return;
    lv_obj_add_flag(lane_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);

    const lv_image_dsc_t *src = &icon_straight;
    const char *turn = "ĐI THẲNG";
    switch (nav->turn) {
        case HUD_TURN_LEFT:         src = &icon_turn_left;    turn = "RẼ TRÁI";     break;
        case HUD_TURN_RIGHT:        src = &icon_turn_right;   turn = "RẼ PHẢI";     break;
        case HUD_TURN_SLIGHT_LEFT:  src = &icon_slight_left;  turn = "CHẾCH TRÁI";  break;
        case HUD_TURN_SLIGHT_RIGHT: src = &icon_slight_right; turn = "CHẾCH PHẢI";  break;
        case HUD_TURN_SHARP_LEFT:   src = &icon_sharp_left;   turn = "RẼ GẮT TRÁI"; break;
        case HUD_TURN_SHARP_RIGHT:  src = &icon_sharp_right;  turn = "RẼ GẮT PHẢI"; break;
        case HUD_TURN_U_TURN:       src = &icon_u_turn;       turn = "QUAY ĐẦU";    break;
        case HUD_TURN_MERGE:        src = &icon_merge;        turn = "NHẬP LÀN";    break;
        case HUD_TURN_EXIT_RIGHT:   src = &icon_exit_right;   turn = "RA LỐI RẼ";   break;
        case HUD_TURN_ROUNDABOUT:   src = &icon_roundabout;   turn = "VÒNG XUYẾN";  break;
        case HUD_TURN_ARRIVE:       src = &icon_arrive;       turn = "ĐẾN ĐÍCH";    break;
        case HUD_TURN_STRAIGHT:
        default: break;
    }
    lv_image_set_src(nav_icon, src);
    icon_tint(nav_icon, HUD_C_CYAN);
    lv_label_set_text(nav_turn, turn);

    /* Cac truong da la chuoi hoan chinh (don vi/dinh dang tu Google Maps
     * qua dien thoai) - hien thi truc tiep, khong tu ghep don vi nua. */
    lv_label_set_text(nav_dist, nav->dist ? nav->dist : "--");
    lv_label_set_text(nav_street, nav->street ? nav->street : "");
    lv_label_set_text(nav_hint,   nav->hint   ? nav->hint   : "");
    lv_label_set_text(m_remain, nav->remain ? nav->remain : "--");
    lv_label_set_text(m_eta, nav->time_remaining ? nav->time_remaining : "--");
    lv_label_set_text(m_arrive, nav->arrive_hhmm ? nav->arrive_hhmm : "--:--");
}

void hud_nav_stop(void)
{
    lv_obj_add_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lane_panel, LV_OBJ_FLAG_HIDDEN);
}
