# LVGL HUD 320x240 (LVGL 9.x, ESP32 + Arduino, RGB565)

Code port of `HUD 320x240.dc.html`. Panel is exactly 320 x 240 (`HUD_SCR_W/H`).
Left column 150 px (speed + limit sign + 2 upcoming road signs + 5-day
forecast), 1 px divider, right half 169 px. Horizontal padding is 5 px
throughout (`PAD` in `hud_ui.c`).

The map is present in **both** modes: a 159 x 136 inset in navigation (below the
manoeuvre block, running to the bottom edge) and full-height when no route is active.

## Files
| File | Purpose |
|---|---|
| `hud_theme.h` | colours, geometry, font macros |
| `hud_ui.h/.c` | screen build + runtime API |
| `hud_signs.h/.c` | 8 road signs (34x34 RGB565A8) + 6 weather glyphs (17x17 A8) |
| `hud_map_assets.h/.c` | map tile, position arrow, north needle |
| `hud_icons.h/.c` | 12 direction arrows + 4 legacy warning glyphs, A8 (recolourable) |
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
   #define LV_FONT_MONTSERRAT_34 1
   #define LV_FONT_MONTSERRAT_40 1
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
| `hud_num_50` | Barlow Condensed SemiBold | 50 | digits (current speed) |
| `hud_num_39` | Barlow Condensed Bold | 39 | digits (speed-limit sign) |
| `hud_num_34` | Barlow Condensed SemiBold | 34 | digits, `,` space, `km` `m` |
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

hud_set_sign(0, HUD_SIGN_SPEEDCAM, 400);     /* slot 0 = nearest, pulses */
hud_set_sign(1, HUD_SIGN_PEDESTRIAN, 1200);
hud_clear_signs();

hud_set_forecast(0, "NAY", HUD_WX_SUN, 33);  /* day 0 = today, cyan label */
hud_set_forecast(1, "T6", HUD_WX_RAIN, 28);

hud_nav_t nav = { HUD_TURN_RIGHT, 300, "Nguyen Trai", "Di lan ben phai...", 84, 12, "14:32" };
hud_set_nav(&nav);                  /* navigation half */
hud_nav_stop();                     /* map view instead */
hud_map_set_street("Tran Duy Hung");
hud_map_set_scale("200 m");
hud_map_set_heading(0);             /* rotates the position arrow */
```

## Upcoming road signs
Two slots on one 140 px row, each 34 px sign + distance stacked to its right
(number 16 px, unit 9 px). Signs are drawn in their real shape and colours,
so they are **RGB565A8 and must not be recoloured**:

| Symbol | Shape |
|---|---|
| `sign_speedcam`, `sign_no_overtake`, `sign_no_horn` | white disc, red ring, black glyph |
| `sign_pedestrian`, `sign_sharp_curve`, `sign_rough_road`, `sign_children`, `sign_traffic_light` | yellow triangle, red border, black glyph |

Slot 0 pulses between full and 50 % opacity on an 800 ms `lv_anim`.
Distances under 1000 m render as `400` + `m`, above as `1,2` + `km`. To add a
sign, drop an SVG in `assets/svg/`, re-run the converter, and extend
`hud_sign_t` plus the switch in `hud_set_sign()`.

## 5-day forecast
Bottom of the left column: five 26 px columns (28 px pitch) with a weekday
label in a **fixed 10 px box**, a 17 px glyph and the daily high. The fixed
label box is deliberate - it keeps all five icons and temperatures on one
baseline even when a label is longer. Day 0's label is cyan. Glyphs are A8 and
tinted per condition: sun/partly/storm amber, rain cyan, cloud/fog grey.

## Speed limit sign
75 px disc with an 8 px red ring and a 39 px number - 1.7x the original 44 px
sign, and now the largest element on the left. The current speed drops to
`HUD_F_SPEED_SM` (50 px) with `km/h` on its own line beneath it so the two
still fit the 140 px content width; the sign row is 88 px tall, which is why
the road-sign and forecast rows below it shifted down 14 px.

## Map view
`map_tile` (full-height, no route) and `map_tile_nav` (159 x 136 inset, shown
during navigation) are separate images. Only `map_tile_nav` carries a bright
cyan route ahead of the vehicle - that is guidance and belongs to navigation.
`map_tile` has the dashed breadcrumb only, ending at the marker, so the
no-navigation view never draws a path the driver has not taken.
`map_arrow` is the position marker at
`HUD_MAP_POS_X/Y` (60, 178); it rotates about its own centre via
`hud_map_set_heading()`. The travelled path is a thin dashed cyan breadcrumb,
deliberately quieter than the navigation route so the two views don't read
alike.

**Both tiles are schematic, not real geography.** `map_tile` is 169x240
(79 kB flash) and `map_tile_nav` is 159x136 (42 kB), both with an invented
road grid, shipped so the view has something to show. It does not correspond to any real place, which is why
`hud_map_set_street()` starts empty rather than naming a street the geometry
does not match. Replace it before shipping, one of:

1. **Vector road data on device** - the honest option. Load a clipped road
   network for your region (OSM extract converted to a compact binary of line
   segments in local coordinates), then draw it per frame with `lv_line` or
   into an `lv_canvas`. Drop `map_tile` and reclaim the 79 kB.
2. **Raster tiles from a companion app** - phone or head unit fetches the tile
   and pushes a 169x240 RGB565 buffer over the link; `lv_image_set_src()` on
   `map_bg` with a runtime `lv_image_dsc_t`. Needs the tile provider's
   attribution shown somewhere in the product.

Online tile services cannot be used offline on the MCU, so option 1 is the
only one that works with no connectivity.

## Not included
- No display-driver tuning for a specific panel (TFT_eSPI `User_Setup.h` is yours).
- No GPS/OBD data source; the sketch feeds demo values.
- Fonts must be generated locally (see above).
