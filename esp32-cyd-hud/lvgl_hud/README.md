# LVGL HUD 320x240 (LVGL 9.x, ESP32 + Arduino, RGB565)

Code port of `HUD 320x240.dc.html`. Same geometry: left column 150 px
(speed + limit sign + 2 warnings), 1 px divider, right half 169 px
(navigation, or the lane-keeping animation when there is no route).

## Files
| File | Purpose |
|---|---|
| `hud_theme.h` | colours, geometry, font macros |
| `hud_ui.h/.c` | screen build + runtime API |
| `hud_lane_assets.h/.c` | car sprite + lane dash sprites for the lane-keeping animation |
| `hud_icons.h/.c` | 16 icons as `lv_image_dsc_t`, `LV_COLOR_FORMAT_A8` (recolourable) |
| `hud_320x240.ino` | Arduino sketch: TFT_eSPI display driver + demo state |
| `assets/png/*.png` | same icons as PNG, if you prefer to re-convert yourself |
| `assets/svg/*.svg` | icon sources |

## Build
1. Arduino libraries: **lvgl 9.x**, **TFT_eSPI** (configure `User_Setup.h` for your panel).
2. Copy `lv_conf_template.h` -> `lv_conf.h`, then set:
   ```c
   #define LV_COLOR_DEPTH 16
   #define LV_USE_DRAW_SW 1
   #define LV_FONT_MONTSERRAT_10 1   /* only while HUD_FONTS_STUB == 1 */
   #define LV_FONT_MONTSERRAT_12 1
   #define LV_FONT_MONTSERRAT_14 1
   #define LV_FONT_MONTSERRAT_16 1
   #define LV_FONT_MONTSERRAT_24 1
   #define LV_FONT_MONTSERRAT_38 1
   #define LV_FONT_MONTSERRAT_48 1
   ```
   If your panel needs byte-swapped RGB565, keep `tft.pushColors(..., true)`
   as written (that flag does the swap) or set `LV_COLOR_16_SWAP` equivalent
   in your port.
3. Put all files in the sketch folder and flash. RAM cost of the draw buffer:
   320 x 40 x 2 = 25.6 kB.

## Fonts (do this to get Vietnamese diacritics)
The design uses **Barlow Condensed** (numbers) and **Chakra Petch** (text).
I cannot run `lv_font_conv` here, so `hud_theme.h` ships with
`HUD_FONTS_STUB 1` (Montserrat fallback, no diacritics). Generate the real
fonts, then set `HUD_FONTS_STUB 0`.

```sh
lv_font_conv --font BarlowCondensed-SemiBold.ttf --size 62 --bpp 4 \
  --format lvgl --lv-include lvgl.h -o hud_num_62.c --force-fast-kern-format \
  -r 0x30-0x39 -r 0x2C -r 0x2E -r 0x20
```

Sizes and character ranges required:

| Font symbol | Family | Size | Ranges |
|---|---|---|---|
| `hud_num_62` | Barlow Condensed SemiBold | 62 | digits `0-9`, `,` `.` space |
| `hud_num_38` | Barlow Condensed SemiBold | 38 | digits, `,` space, `km` `m` |
| `hud_num_24` | Barlow Condensed Bold | 24 | digits |
| `hud_num_16` | Barlow Condensed SemiBold | 16 | digits, `,` `:` space, `km` `ph` |
| `hud_text_13` | Chakra Petch SemiBold | 13 | Latin + Vietnamese subset |
| `hud_text_11` | Chakra Petch Regular | 11 | Latin + Vietnamese subset |
| `hud_label_9` | Chakra Petch Medium | 9-10 | uppercase Latin + Vietnamese subset |

Vietnamese subset for the three text fonts (add to the command as `-r`):

```
-r 0x20-0x7F -r 0xC0-0xC3 -r 0xC8-0xCA -r 0xCC-0xCD -r 0xD2-0xD5 -r 0xD9-0xDA -r 0xDD \
-r 0xE0-0xE3 -r 0xE8-0xEA -r 0xEC-0xED -r 0xF2-0xF5 -r 0xF9-0xFA -r 0xFD \
-r 0x102-0x103 -r 0x110-0x111 -r 0x128-0x129 -r 0x168-0x169 -r 0x1A0-0x1A1 \
-r 0x1AF-0x1B0 -r 0x1EA0-0x1EF9
```

All literal strings in `hud_ui.c` are currently unaccented ASCII so the stub
build is readable. Once the real fonts are in, replace them with the accented
Vietnamese from the mockup ("TỐC ĐỘ", "CẢNH BÁO TIẾP", "RẼ PHẢI", "GIỮ LÀN",
"KHÔNG DẪN ĐƯỜNG", "BẮN TỐC ĐỘ", "NGƯỜI ĐI BỘ", "ĐƯỜNG XẤU", "CUA GẤP",
"CÒN LẠI", "T.GIAN", "ĐẾN", "phút") and save the file as UTF-8.

## API
```c
hud_ui_init();
hud_set_speed_limit(60);            /* 0 = unknown, hides the sign */
hud_set_speed(62);                  /* > limit -> number turns red */
hud_set_warning(0, HUD_WARN_SPEEDCAM, 400);
hud_set_warning(1, HUD_WARN_PEDESTRIAN, 1200);
hud_clear_warnings();

hud_nav_t nav = { HUD_TURN_RIGHT, 300, "Nguyen Trai", "Di lan ben phai...", 84, 12, "14:32" };
hud_set_nav(&nav);                  /* navigation half */
hud_nav_stop();                     /* lane-keeping animation instead */
```

Warning slot 0 is drawn amber/urgent for `HUD_WARN_SPEEDCAM`, neutral grey
otherwise. Distances < 1000 render as `m`, above as `x.y km`.

## Icons
A8 (alpha-only) format: 1 byte per pixel, tinted at runtime with
`lv_obj_set_style_image_recolor()`. 4 warnings at 30x26 + 12 direction arrows
at 52x52 = ~35 kB flash. Drop the arrows you do not use to save space.

Direction set, one per `hud_turn_t` value: `straight`, `turn_left`,
`turn_right`, `slight_left`, `slight_right`, `sharp_left`, `sharp_right`,
`u_turn`, `merge`, `exit_right`, `roundabout`, `arrive`. To use RGB565A8 or a different format instead, re-convert
`assets/png/*.png` with the LVGL online image converter.

## Lane-keeping animation resources
`hud_lane_assets.c` holds three sprites and the geometry defines used by
`build_lane()`:

| Symbol | Size | Format | Flash | Note |
|---|---|---|---|---|
| `car_top` | 40 x 64 | RGB565A8 | 7.7 kB | cyan gradient body, dark glass, mirrors — colour baked in |
| `lane_dash` | 2 x 14 | A8 | 28 B | side markings, soft ends; recoloured to `HUD_C_LANE` |
| `lane_dash_center` | 1 x 8 | A8 | 8 B | faint centre line; recoloured to `HUD_C_LINE` |

Two `lv_anim`s drive it, no frame sequence: the three dash tracks are 2x the
panel height and translate up by one `HUD_LANE_PERIOD` (30 px) in 1100 ms on
infinite repeat, so the loop is seamless; the car sways +/- `HUD_CAR_SWAY`
(3 px) over 3400 ms with `lv_anim_path_ease_in_out`. Speed of the road reads
as motion, so tie the 1100 ms to real speed if you want:
`lv_anim_set_duration(&lane_anim, 60000 / kmh)`.

Re-render `car_top` at a different size by editing
`assets/png/car_top.png` and re-running the converter; keep RGB565A8 if you
want the gradient, or switch to A8 and recolour if you prefer a flat car.

## Not included
- No display-driver tuning for a specific panel (TFT_eSPI `User_Setup.h` is yours).
- No GPS/OBD data source; the sketch feeds demo values.
- Fonts must be generated locally (see above).
