# Font cho HUD (LVGL 9.x)

Bộ font dùng cho `hud_theme.h`/`hud_ui.c` (thiết kế trong `lvgl_hud/`) —
**Barlow Condensed** cho số (đồng hồ tốc độ kiểu HUD, cao/hẹp) và
**Chakra Petch** cho chữ (phong cách kỹ thuật/đua xe) — cả hai đều lấy TTF
tĩnh thật từ kho Google Fonts chính thức, phát hành theo **SIL Open Font
License 1.1**.

| File | Font gốc | Size | Dải ký tự |
|---|---|---|---|
| `hud_num_62.c` | Barlow Condensed SemiBold | 62 | số `0-9`, `,` `.` space |
| `hud_num_38.c` | Barlow Condensed SemiBold | 38 | số, `,` `.` space, `k` `m` |
| `hud_num_24.c` | Barlow Condensed Bold | 24 | số `0-9` |
| `hud_num_16.c` | Barlow Condensed SemiBold | 16 | số, `,` `:` space, `k` `m` `p` `h` |
| `hud_text_13.c` | Chakra Petch SemiBold | 13 | Latin + tiếng Việt |
| `hud_text_11.c` | Chakra Petch Regular | 11 | Latin + tiếng Việt |
| `hud_label_9.c` | Chakra Petch Medium | 10 | Latin + tiếng Việt |

QUAN TRỌNG (bài học từ lần build font tiếng Việt trước trong project này):
- Luôn dùng **TTF tĩnh thật** (`github.com/google/fonts/raw/main/ofl/...`),
  KHÔNG dùng bản `.woff` qua npm package `@fontsource/*` — đã xác nhận thực
  tế bản đó là font variable, `lv_font_conv` đọc sai gần hết glyph.
- Sau khi build, PHẢI bật `#define LV_USE_FONT_COMPRESSED 1` trong
  `lv_conf.h` — font `lv_font_conv` xuất ra ở dạng nén (RLE,
  `bitmap_format=1`), thiếu cờ này chữ sẽ render RỖNG HOÀN TOÀN (không phải
  ô vuông) dù build không báo lỗi gì.

Build lại nếu cần đổi size/font khác:

```sh
npm install lv_font_conv@1.5.3
curl -LO https://github.com/google/fonts/raw/main/ofl/barlowcondensed/BarlowCondensed-SemiBold.ttf
curl -LO https://github.com/google/fonts/raw/main/ofl/barlowcondensed/BarlowCondensed-Bold.ttf
curl -LO https://github.com/google/fonts/raw/main/ofl/chakrapetch/ChakraPetch-SemiBold.ttf
curl -LO https://github.com/google/fonts/raw/main/ofl/chakrapetch/ChakraPetch-Regular.ttf
curl -LO https://github.com/google/fonts/raw/main/ofl/chakrapetch/ChakraPetch-Medium.ttf

VN="0x20-0x7F,0xC0-0xC3,0xC8-0xCA,0xCC-0xCD,0xD2-0xD5,0xD9-0xDA,0xDD,0xE0-0xE3,0xE8-0xEA,0xEC-0xED,0xF2-0xF5,0xF9-0xFA,0xFD,0x102-0x103,0x110-0x111,0x128-0x129,0x168-0x169,0x1A0-0x1A1,0x1AF-0x1B0,0x1EA0-0x1EF9"

npx lv_font_conv --font BarlowCondensed-SemiBold.ttf --size 62 --bpp 4 --format lvgl --lv-include lvgl.h -o hud_num_62.c -r 0x30-0x39 -r 0x2C -r 0x2E -r 0x20
npx lv_font_conv --font ChakraPetch-SemiBold.ttf --size 13 --bpp 4 --format lvgl --lv-include lvgl.h -o hud_text_13.c -r "$VN"
```

Sau khi build font mới, kiểm tra nhanh bằng `grep -n "range_start\|list_length\|bitmap_format" file.c` —
phải thấy vài dòng `LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY` với `range_length`
khớp đúng số ký tự yêu cầu (không phải toàn `SPARSE_TINY` với `list_length`
nhỏ bất thường — dấu hiệu font nguồn bị đọc sai).
