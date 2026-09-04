/* hud_ui.c - 320x240 HUD.
 * Left 150 px : speed, speed-limit sign, two upcoming road signs, 5-day forecast.
 * Right 169 px: navigation, or the map view when no route is active.
 * Padding is 5 px left/right everywhere.
 */
#include "hud_ui.h"
#include "hud_icons.h"
#include "hud_signs.h"
#include "hud_map_assets.h"
#include <stdio.h>
#include <string.h>

#define PAD 5

static lv_obj_t *scr, *left_col;
static lv_obj_t *lbl_speed, *sign_limit, *lbl_limit;
static lv_obj_t *sign_img[2], *sign_num[2], *sign_unit[2];
static lv_obj_t *wx_icon[HUD_WX_DAYS], *wx_lbl[HUD_WX_DAYS], *wx_temp[HUD_WX_DAYS];

static lv_obj_t *nav_panel, *nav_icon, *nav_dist, *nav_turn, *nav_street, *nav_hint;
static lv_obj_t *nav_map, *nav_marker;
static lv_obj_t *m_remain, *m_eta, *m_arrive;

static lv_obj_t *map_panel, *map_bg, *map_marker, *map_street, *map_scale;
static lv_anim_t sign_pulse;

static uint16_t cur_speed = 0, cur_limit = 0;
static bool nav_active = false;

/* ---------- helpers ---------- */
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

/* "400" + "m", or "1,2" + "km" */
static void fmt_dist(uint16_t m, char *num, size_t n_num, const char **unit)
{
    if (m >= 1000) { snprintf(num, n_num, "%u,%u", m / 1000, (m % 1000) / 100); *unit = "km"; }
    else           { snprintf(num, n_num, "%u", (unsigned)m);                   *unit = "m";  }
}

static void pulse_exec_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* ---------- left column ---------- */
static void build_left(void)
{
    lv_obj_t *col = plain(scr);
    left_col = col;
    lv_obj_set_size(col, HUD_LEFT_W, HUD_SCR_H);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_pad_hor(col, PAD, 0);
    lv_obj_set_style_pad_ver(col, 8, 0);

    /* Speed limit sign is 1.7x the old 44 px disc -> 75 px, border 8 px.
     * The speed number drops to HUD_F_SPEED_SM and puts km/h on its own line
     * so the pair still fits the 140 px content width. */
    lv_obj_t *cap_speed = caption(col, "TOC DO");
    lv_obj_align(cap_speed, LV_ALIGN_TOP_LEFT, 0, 14);

    lbl_speed = label(col, "0", HUD_F_SPEED_SM, HUD_C_WHITE);
    lv_obj_align(lbl_speed, LV_ALIGN_TOP_LEFT, -2, 24);

    lv_obj_t *unit = label(col, "km/h", HUD_F_LABEL, HUD_C_TEXT_DIM);
    lv_obj_align(unit, LV_ALIGN_TOP_LEFT, 0, 68);

    sign_limit = plain(col);
    lv_obj_set_size(sign_limit, 75, 75);
    lv_obj_align(sign_limit, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(sign_limit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sign_limit, HUD_C_WHITE, 0);
    lv_obj_set_style_bg_opa(sign_limit, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sign_limit, HUD_C_RED, 0);
    lv_obj_set_style_border_width(sign_limit, 8, 0);
    lbl_limit = label(sign_limit, "--", HUD_F_LIMIT_LG, HUD_C_BG);
    lv_obj_center(lbl_limit);

    lv_obj_t *hr = plain(col);
    lv_obj_set_size(hr, HUD_LEFT_W - 2 * PAD, 1);
    lv_obj_align(hr, LV_ALIGN_TOP_LEFT, 0, 88);
    lv_obj_set_style_bg_color(hr, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);

    lv_obj_t *cap_sign = caption(col, "BIEN BAO TIEP");
    lv_obj_align(cap_sign, LV_ALIGN_TOP_LEFT, 0, 94);

    /* Two sign groups on a 140 px row. Each group is 34 px sign + 4 px gap +
     * 26 px number block = 64 px, so 2 x 64 = 128 fits with 12 px to spare;
     * the groups are pinned to the two ends instead of using an inter-group
     * gap, which is what used to overflow the column. */
    for (int i = 0; i < 2; i++) {
        int gx = (i == 0) ? 0 : (HUD_LEFT_W - 2 * PAD - 64);

        sign_img[i] = lv_image_create(col);
        lv_image_set_src(sign_img[i], &sign_speedcam);
        lv_obj_align(sign_img[i], LV_ALIGN_TOP_LEFT, gx, 106);

        sign_num[i] = label(col, "0", HUD_F_METRIC, HUD_C_WHITE);
        lv_obj_align(sign_num[i], LV_ALIGN_TOP_LEFT, gx + 38, 109);

        sign_unit[i] = label(col, "m", HUD_F_LABEL, HUD_C_TEXT_DIM);
        lv_obj_align(sign_unit[i], LV_ALIGN_TOP_LEFT, gx + 38, 126);

        lv_obj_add_flag(sign_img[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sign_num[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sign_unit[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* nearest sign pulses */
    lv_anim_init(&sign_pulse);
    lv_anim_set_var(&sign_pulse, sign_img[0]);
    lv_anim_set_exec_cb(&sign_pulse, pulse_exec_cb);
    lv_anim_set_values(&sign_pulse, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_duration(&sign_pulse, 800);
    lv_anim_set_reverse_duration(&sign_pulse, 800);
    lv_anim_set_repeat_count(&sign_pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&sign_pulse);

    /* ---- 5-day forecast strip ----
     * 5 columns of 26 px across 140 px. Every label has the same fixed 10 px
     * box so all five icons and temperatures share one baseline. */
    lv_obj_t *hr2 = plain(col);
    lv_obj_set_size(hr2, HUD_LEFT_W - 2 * PAD, 1);
    lv_obj_align(hr2, LV_ALIGN_BOTTOM_LEFT, 0, -46);
    lv_obj_set_style_bg_color(hr2, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(hr2, LV_OPA_COVER, 0);

    for (int i = 0; i < HUD_WX_DAYS; i++) {
        int cx = i * 28;   /* 5 x 26 + 4 x 2 gap = 138 */

        wx_lbl[i] = plain(col);
        lv_obj_set_size(wx_lbl[i], 26, 10);
        lv_obj_align(wx_lbl[i], LV_ALIGN_BOTTOM_LEFT, cx, -36);
        lv_obj_t *t = label(wx_lbl[i], "--", HUD_F_LABEL, HUD_C_LABEL);
        lv_obj_set_style_text_letter_space(t, 1, 0);
        lv_obj_center(t);

        wx_icon[i] = lv_image_create(col);
        lv_image_set_src(wx_icon[i], &wx_sun);
        lv_obj_align(wx_icon[i], LV_ALIGN_BOTTOM_LEFT, cx + 5, -18);
        icon_tint(wx_icon[i], HUD_C_AMBER);

        wx_temp[i] = label(col, "--", HUD_F_SMALL, HUD_C_TEXT);
        lv_obj_set_size(wx_temp[i], 26, 13);
        lv_obj_set_style_text_align(wx_temp[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(wx_temp[i], LV_ALIGN_BOTTOM_LEFT, cx, -2);
    }
}

/* ---------- navigation half ---------- */
static void metric(lv_obj_t *parent, const char *cap_txt, int x, lv_align_t al, lv_color_t vc, lv_obj_t **out_val)
{
    lv_obj_t *g = plain(parent);
    lv_obj_set_size(g, 54, 26);
    lv_obj_align(g, al, x, 0);
    lv_obj_t *c = caption(g, cap_txt);
    lv_obj_align(c, al == LV_ALIGN_BOTTOM_RIGHT ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    *out_val = label(g, "--", HUD_F_METRIC, vc);
    lv_obj_align(*out_val, al == LV_ALIGN_BOTTOM_RIGHT ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, 0, 0);
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
    lv_obj_set_style_pad_hor(nav_panel, PAD, 0);
    lv_obj_set_style_pad_ver(nav_panel, 8, 0);

    /* Trip metrics are the panel HEADER: remaining / time / arrival on one
     * row at the top, closed by a 1 px rule. Everything else sits below it. */
    metric(nav_panel, "CON LAI", 0,  LV_ALIGN_TOP_LEFT,  HUD_C_TEXT, &m_remain);
    metric(nav_panel, "T.GIAN",  54, LV_ALIGN_TOP_LEFT,  HUD_C_TEXT, &m_eta);
    metric(nav_panel, "DEN",     0,  LV_ALIGN_TOP_RIGHT, HUD_C_CYAN, &m_arrive);

    lv_obj_t *hr = plain(nav_panel);
    lv_obj_set_size(hr, HUD_RIGHT_W - 2 * PAD, 1);
    lv_obj_align(hr, LV_ALIGN_TOP_LEFT, 0, 31);
    lv_obj_set_style_bg_color(hr, HUD_C_LINE, 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);

    nav_icon = lv_image_create(nav_panel);
    lv_image_set_src(nav_icon, &icon_turn_right);
    lv_obj_align(nav_icon, LV_ALIGN_TOP_LEFT, 0, 38);
    icon_tint(nav_icon, HUD_C_CYAN);

    nav_dist = label(nav_panel, "0 m", HUD_F_DIST, HUD_C_WHITE);
    lv_obj_align(nav_dist, LV_ALIGN_TOP_LEFT, 53, 38);

    nav_turn = label(nav_panel, "", HUD_F_LABEL, HUD_C_CYAN);
    lv_obj_set_style_text_letter_space(nav_turn, 1, 0);
    lv_obj_align(nav_turn, LV_ALIGN_TOP_LEFT, 53, 74);

    /* Map inset: directly under the manoeuvre block, running to the bottom
     * edge. Street name and lane hint are overlay chips so the map gets the
     * full HUD_MAP_NAV_H band. */
    nav_map = lv_image_create(nav_panel);
    lv_image_set_src(nav_map, &map_tile_nav);
    lv_obj_align(nav_map, LV_ALIGN_TOP_LEFT, 0, HUD_MAP_NAV_TOP);

    nav_marker = lv_image_create(nav_panel);
    lv_image_set_src(nav_marker, &map_arrow);
    lv_obj_align(nav_marker, LV_ALIGN_TOP_LEFT,
                 HUD_MAP_NAV_POS_X - 10, HUD_MAP_NAV_TOP + HUD_MAP_NAV_POS_Y - 11);
    lv_image_set_pivot(nav_marker, 10, 11);

    nav_street = label(nav_panel, "", HUD_F_SMALL, HUD_C_TEXT);
    lv_obj_align(nav_street, LV_ALIGN_TOP_LEFT, 4, HUD_MAP_NAV_TOP + 4);
    lv_label_set_long_mode(nav_street, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(nav_street, HUD_C_BG, 0);
    lv_obj_set_style_bg_opa(nav_street, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(nav_street, 3, 0);
    lv_obj_set_style_radius(nav_street, 2, 0);

    nav_hint = label(nav_panel, "", HUD_F_LABEL, HUD_C_TEXT_DIM);
    lv_obj_align(nav_hint, LV_ALIGN_TOP_RIGHT, -4, HUD_MAP_NAV_TOP + HUD_MAP_NAV_H - 14);
    lv_label_set_long_mode(nav_hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(nav_hint, HUD_C_BG, 0);
    lv_obj_set_style_bg_opa(nav_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(nav_hint, 3, 0);
    lv_obj_set_style_radius(nav_hint, 2, 0);

    lv_obj_add_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- map half (shown when no route is active) ---------- */
static void build_map(void)
{
    map_panel = plain(scr);
    lv_obj_set_size(map_panel, HUD_RIGHT_W, HUD_SCR_H);
    lv_obj_set_pos(map_panel, HUD_LEFT_W + 1, 0);
    lv_obj_set_style_bg_color(map_panel, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(map_panel, LV_OPA_COVER, 0);

    /* pre-rendered map tile: blocks, roads and the travelled route */
    map_bg = lv_image_create(map_panel);
    lv_image_set_src(map_bg, &map_tile);
    lv_obj_align(map_bg, LV_ALIGN_TOP_LEFT, 0, 0);

    /* own position: rotatable arrow head */
    map_marker = lv_image_create(map_panel);
    lv_image_set_src(map_marker, &map_arrow);
    lv_obj_align(map_marker, LV_ALIGN_TOP_LEFT, HUD_MAP_POS_X - 10, HUD_MAP_POS_Y - 11);
    lv_image_set_pivot(map_marker, 10, 11);

    lv_obj_t *north = lv_image_create(map_panel);
    lv_image_set_src(north, &map_north);
    lv_obj_align(north, LV_ALIGN_TOP_LEFT, PAD, 7);
    icon_tint(north, HUD_C_TEXT_DIM);

    lv_obj_t *ncap = caption(map_panel, "BAC");
    lv_obj_align(ncap, LV_ALIGN_TOP_LEFT, PAD + 12, 7);
    lv_obj_set_style_text_color(ncap, HUD_C_TEXT_DIM, 0);

    map_street = label(map_panel, "", HUD_F_SMALL, HUD_C_TEXT);
    lv_obj_align(map_street, LV_ALIGN_TOP_RIGHT, -PAD, 6);
    lv_obj_set_style_bg_color(map_street, HUD_C_BG, 0);
    lv_obj_set_style_bg_opa(map_street, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(map_street, 3, 0);
    lv_obj_set_style_pad_ver(map_street, 1, 0);
    lv_obj_set_style_radius(map_street, 3, 0);

    lv_obj_t *bar = plain(map_panel);
    lv_obj_set_size(bar, 34, 1);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, PAD, -18);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x5C6773), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    map_scale = label(map_panel, "200 m", HUD_F_LABEL, lv_color_hex(0x5C6773));
    lv_obj_align(map_scale, LV_ALIGN_BOTTOM_LEFT, PAD, -6);

    lv_obj_t *foot = caption(map_panel, "KHONG DAN DUONG");
    lv_obj_align(foot, LV_ALIGN_BOTTOM_RIGHT, -PAD, -6);
    lv_obj_set_style_text_color(foot, lv_color_hex(0x5C6773), 0);
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
    build_map();
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

void hud_set_sign(uint8_t slot, hud_sign_t sign, uint16_t dist_m)
{
    if (slot > 1) return;

    if (sign == HUD_SIGN_NONE) {
        lv_obj_add_flag(sign_img[slot],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sign_num[slot],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sign_unit[slot], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const lv_image_dsc_t *src;
    switch (sign) {
        case HUD_SIGN_SPEEDCAM:      src = &sign_speedcam;      break;
        case HUD_SIGN_NO_OVERTAKE:   src = &sign_no_overtake;   break;
        case HUD_SIGN_NO_HORN:       src = &sign_no_horn;       break;
        case HUD_SIGN_PEDESTRIAN:    src = &sign_pedestrian;    break;
        case HUD_SIGN_SHARP_CURVE:   src = &sign_sharp_curve;   break;
        case HUD_SIGN_ROUGH_ROAD:    src = &sign_rough_road;    break;
        case HUD_SIGN_CHILDREN:      src = &sign_children;      break;
        case HUD_SIGN_TRAFFIC_LIGHT: src = &sign_traffic_light; break;
        default: return;
    }
    lv_image_set_src(sign_img[slot], src);

    char num[8];
    const char *unit;
    fmt_dist(dist_m, num, sizeof(num), &unit);
    lv_label_set_text(sign_num[slot], num);
    lv_label_set_text(sign_unit[slot], unit);

    lv_obj_clear_flag(sign_img[slot],  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(sign_num[slot],  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(sign_unit[slot], LV_OBJ_FLAG_HIDDEN);
}

void hud_clear_signs(void)
{
    hud_set_sign(0, HUD_SIGN_NONE, 0);
    hud_set_sign(1, HUD_SIGN_NONE, 0);
}

void hud_set_forecast(uint8_t day, const char *label_txt, hud_wx_t cond, int8_t temp_c)
{
    if (day >= HUD_WX_DAYS) return;

    lv_obj_t *t = lv_obj_get_child(wx_lbl[day], 0);
    lv_label_set_text(t, label_txt ? label_txt : "--");
    lv_obj_set_style_text_color(t, (day == 0) ? HUD_C_CYAN : HUD_C_LABEL, 0);

    const lv_image_dsc_t *src = &wx_sun;
    lv_color_t tint = HUD_C_AMBER;
    switch (cond) {
        case HUD_WX_SUN:    src = &wx_sun;    tint = HUD_C_AMBER;   break;
        case HUD_WX_PARTLY: src = &wx_partly; tint = HUD_C_AMBER;   break;
        case HUD_WX_CLOUD:  src = &wx_cloud;  tint = HUD_C_TEXT_DIM; break;
        case HUD_WX_RAIN:   src = &wx_rain;   tint = HUD_C_CYAN;    break;
        case HUD_WX_STORM:  src = &wx_storm;  tint = HUD_C_AMBER;   break;
        case HUD_WX_FOG:    src = &wx_fog;    tint = HUD_C_TEXT_DIM; break;
    }
    lv_image_set_src(wx_icon[day], src);
    icon_tint(wx_icon[day], tint);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d°", (int)temp_c);
    lv_label_set_text(wx_temp[day], buf);
}

void hud_set_nav(const hud_nav_t *nav)
{
    if (!nav) return;
    nav_active = true;
    lv_obj_add_flag(map_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);

    const lv_image_dsc_t *src = &icon_straight;
    const char *turn = "DI THANG";
    switch (nav->turn) {
        case HUD_TURN_LEFT:         src = &icon_turn_left;    turn = "RE TRAI";     break;
        case HUD_TURN_RIGHT:        src = &icon_turn_right;   turn = "RE PHAI";     break;
        case HUD_TURN_SLIGHT_LEFT:  src = &icon_slight_left;  turn = "CHECH TRAI";  break;
        case HUD_TURN_SLIGHT_RIGHT: src = &icon_slight_right; turn = "CHECH PHAI";  break;
        case HUD_TURN_SHARP_LEFT:   src = &icon_sharp_left;   turn = "RE GAP TRAI"; break;
        case HUD_TURN_SHARP_RIGHT:  src = &icon_sharp_right;  turn = "RE GAP PHAI"; break;
        case HUD_TURN_U_TURN:       src = &icon_u_turn;       turn = "QUAY DAU";    break;
        case HUD_TURN_MERGE:        src = &icon_merge;        turn = "NHAP LAN";    break;
        case HUD_TURN_EXIT_RIGHT:   src = &icon_exit_right;   turn = "RA LOI RE";   break;
        case HUD_TURN_ROUNDABOUT:   src = &icon_roundabout;   turn = "VONG XUYEN";  break;
        case HUD_TURN_ARRIVE:       src = &icon_arrive;       turn = "DEN DICH";    break;
        case HUD_TURN_STRAIGHT:
        default: break;
    }
    lv_image_set_src(nav_icon, src);
    icon_tint(nav_icon, HUD_C_CYAN);
    lv_label_set_text(nav_turn, turn);

    char num[8], buf[20];
    const char *unit;
    fmt_dist(nav->dist_m, num, sizeof(num), &unit);
    snprintf(buf, sizeof(buf), "%s %s", num, unit);
    lv_label_set_text(nav_dist, buf);

    lv_label_set_text(nav_street, nav->street ? nav->street : "");
    lv_label_set_text(nav_hint,   nav->hint   ? nav->hint   : "");

    snprintf(buf, sizeof(buf), "%u,%u km", nav->remain_100m / 10, nav->remain_100m % 10);
    lv_label_set_text(m_remain, buf);
    snprintf(buf, sizeof(buf), "%u ph", (unsigned)nav->eta_min);
    lv_label_set_text(m_eta, buf);
    lv_label_set_text(m_arrive, nav->arrive_hhmm ? nav->arrive_hhmm : "--:--");
}

void hud_nav_stop(void)
{
    nav_active = false;
    lv_obj_add_flag(nav_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(map_panel, LV_OBJ_FLAG_HIDDEN);
}

void hud_map_set_street(const char *street)
{
    lv_label_set_text(map_street, street ? street : "");
}

void hud_map_set_scale(const char *txt)
{
    lv_label_set_text(map_scale, txt ? txt : "");
}

void hud_map_set_heading(uint16_t deg)
{
    int32_t r = (int32_t)(deg % 360) * 10;   /* 0.1 deg units */
    lv_image_set_rotation(map_marker, r);
    lv_image_set_rotation(nav_marker, r);
}
