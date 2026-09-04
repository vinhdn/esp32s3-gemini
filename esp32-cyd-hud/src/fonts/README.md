# Font tiếng Việt cho LVGL

`lv_font_vi_14/16/20.c` build từ font **Be Vietnam Pro** (weight Regular),
file TTF tĩnh lấy từ kho Google Fonts chính thức
(`google/fonts/ofl/bevietnampro/BeVietnamPro-Regular.ttf`) — font này phát
hành theo **SIL Open Font License 1.1**, dùng/đóng gói lại tự do kể cả trong
firmware nhúng.

QUAN TRỌNG: phải dùng bản **TTF tĩnh** này, KHÔNG dùng bản `.woff` lấy qua npm
package `@fontsource/be-vietnam-pro` — đã kiểm chứng thực tế: bản `.woff` đó
là font variable, `lv_font_conv` (dựa trên fontkit) đọc sai hầu hết glyph
(chỉ ra đúng 16 glyph thay vì đủ ASCII+Latin-1+tiếng Việt), khiến MỌI ký tự
hiển thị thành ô vuông rỗng (glyph .notdef) trên board thật — không phải lỗi
range Unicode hay encoding UTF-8 (cả 2 cái đó đã đúng ngay từ đầu).

Dải ký tự bao gồm: ASCII + Latin-1 + các ký tự riêng tiếng Việt
(Ă/Đ/Ĩ/Ũ/Ơ/Ư và toàn bộ tổ hợp dấu thanh Latin Extended Additional
U+1EA0-1EF9) — đủ hiển thị bất kỳ chuỗi tiếng Việt có dấu nào (tên đường,
hướng dẫn... lấy trực tiếp từ Google Maps qua BLE).

Build lại bằng `lv_font_conv` (https://github.com/lvgl/lv_font_conv) nếu cần
đổi size/font khác:

```sh
npm install lv_font_conv@1.5.3
curl -LO https://github.com/google/fonts/raw/main/ofl/bevietnampro/BeVietnamPro-Regular.ttf
RANGE="0x20-0x7E,0xA0-0xFF,0x102-0x103,0x110-0x111,0x128-0x129,0x168-0x169,0x1A0-0x1A1,0x1AF-0x1B0,0x1EA0-0x1EF9"
npx lv_font_conv --font BeVietnamPro-Regular.ttf -r "$RANGE" --size <SIZE> --format lvgl --bpp 4 --lv-include lvgl.h -o lv_font_vi_<SIZE>.c
```

Sau khi build font mới/đổi range, LUÔN kiểm tra file `.c` sinh ra có đủ
glyph thật (không chỉ vài chục byte comment) — cách nhanh: `grep -c "range_start"`
phải ra vài dòng `LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY` với `range_length` khớp
đúng số ký tự yêu cầu (vd 95 cho dải ASCII 0x20-0x7E), không phải toàn
`SPARSE_TINY` với `list_length` nhỏ bất thường (dấu hiệu font nguồn bị đọc
sai như trường hợp `.woff` ở trên).
