# esp32-cyd-hud

Firmware HUD lái xe cho board **"Cheap Yellow Display" (CYD) ESP32-2432S028R**
(ESP32-WROOM-32 + màn hình ILI9341 2.8" 320x240 SPI + cảm ứng điện trở
XPT2046, không PSRAM) — thay thế board ESP32-S3 1.54" hiện tại (`esp32/`).

Board này **giả lập đúng identity BLE** của board S3 cũ (cùng tên quảng bá,
cùng service/characteristic UUID) nên app Android `com.esp32nav` **không cần
sửa gì** — chỉ cần tắt board S3 cũ và bật board này lên, app tự kết nối và
gửi dữ liệu như bình thường.

## Trước khi flash

1. **Xác nhận lại pinout** trong `include/board_pins.h` (và các macro tương
   ứng trong `platformio.ini`) theo đúng board thật của bạn — pinout trong
   repo là pinout phổ biến nhất cộng đồng CYD dùng, nhưng nhiều lô hàng hàn
   khác nhau (đặc biệt chân backlight `TFT_BL` và IRQ cảm ứng). Nếu màn hình
   không sáng hoặc cảm ứng không nhận, đây là chỗ đầu tiên cần kiểm tra.
2. Nếu màu hiển thị bị âm bản/lệch sau khi flash, thử đổi
   `-DILI9341_2_DRIVER=1` thành `-DILI9341_DRIVER=1` trong `platformio.ini`
   (2 biến thể panel ILI9341 phổ biến trên các lô CYD khác nhau).
3. Cảm ứng chưa hiệu chỉnh (calibration mặc định trong `ui.cpp` chỉ là giá trị
   tạm) — nếu dùng tính năng chạm, chạy ví dụ `TOUCH_calibrate` của thư viện
   TFT_eSPI trên board thật rồi thay 5 số trong `s_tft.setTouch(calib)`.

## Build & flash (PlatformIO)

Máy dev hiện **chưa cài PlatformIO CLI** — cần cài trước (khuyến nghị qua
pipx/pip trong venv, hoặc extension PlatformIO IDE trong VS Code):

```sh
pip install platformio   # hoặc dùng VS Code PlatformIO extension
cd esp32-cyd-hud
pio run                  # build
pio run -t upload        # flash (board cắm USB, driver CP2102 thường tự nhận)
pio device monitor       # xem log (115200 baud)
```

## Kiểm tra sau khi flash

1. Log serial phải thấy NimBLE khởi động và quảng bá tên `VIETMAP_HUD_H50`.
2. Màn hình sáng lên, hiển thị UI mặc định: tốc độ "0", các ô biển báo/camera
   hiện "--" (chưa có dữ liệu), đèn BLE góc trên phải màu xám (chưa kết nối).
3. **Tắt nguồn board ESP32-S3 cũ** (2 board dùng chung tên/UUID nên chỉ 1 cái
   connect được cùng lúc), mở app `com.esp32nav` trên điện thoại đã pair —
   đèn BLE trên màn hình chuyển xanh khi `ImageRelayBle` connect thành công.
4. Lái thử hoặc dùng `mockgps.sh` ở thư mục gốc repo để giả lập — xác nhận số
   tốc độ/biển báo/khoảng cách camera cập nhật đúng theo dữ liệu VMSX thật.

## Kiến trúc

- `include/board_pins.h` — **nơi duy nhất** cần sửa GPIO nếu board khác.
- `include/hud_state.h` / `src/hud_state.cpp` — struct dữ liệu dùng chung
  (mirror frame VMSX), bảo vệ bằng mutex FreeRTOS.
- `src/ble_server.cpp` — GATT server NimBLE, parse frame `VMSX`/`VMSL`/`VHUD`
  y hệt `esp32/main/ble/waze_hud_ble.c` (board S3) để tương thích ngược, cộng
  thêm `VWXF` (dự báo 5 ngày, xem dưới).
- `src/ui.cpp` — display driver (TFT_eSPI làm backend LVGL) + toàn bộ giao
  diện HUD.
- `src/main.cpp` — 1 task FreeRTOS riêng chạy LVGL (core 1); callback BLE
  chạy trên task của NimBLE — 2 bên đồng bộ qua `hud_state_lock()`.

## Dự báo 5 ngày (frame `VWXF`)

Độc lập hoàn toàn với VietMap/VMSX — Android (`WeatherManager.kt` trong
`android_car_nav`) tự fetch Open-Meteo (5 ngày) và gửi qua BLE theo chu kỳ
riêng (`startIndependentBleUpdates()`, khởi động trong
`BleForegroundService.onCreate()`), không phụ thuộc
`VietmapAccessibilityService` có bắt được bong bóng VietMap hay không.

Frame `VWXF` v1 (17 byte, 5 ngày): `magic(4)="VWXF" version(1)=1
dayCount(1)=5 [temp:int8, condition:uint8]×5 checksum(1)=XOR`. `condition`
dùng chung thang 0=nắng,1=mây,2=mưa,3=giông,4=tuyết/sương với VMSX. Parse ở
`parse_vwxf()` trong `ble_server.cpp`, lưu vào `hud_state_t.forecast_*`
(mảng riêng, không đụng `today_/tomorrow_weather_*` cũ vẫn gắn với VMSX).

Hiển thị: 5 cột cuối cột trái (`hud_set_forecast()` trong `hud_ui.c`), nhãn
cố định "NAY/MAI/+2/+3/+4" (board không có RTC nên không tự biết thứ mấy),
chưa có icon riêng (chưa có asset trong build này) — màu số nhiệt độ đổi
theo `condition` thay cho glyph.

## Offline map (đang phát triển, chưa wire vào build này)

`offline_map/` + `tools/map_pipeline/` + `map_data/` — bản đồ vector
roads-only offline, đọc từ microSD, xem `OFFLINE_MAP_FEATURE.md` cho spec
đầy đủ. Đã có: pipeline Python (OSM → `map.idx`/`map.bin`, đã chạy thử cho
Hoàn Kiếm), `Projection`/`TileParser`/`TileStore` + unit test native, và một
`BlePositionProvider` (điện thoại gửi GPS qua BLE, board chỉ render — spec
mục 9). Chưa có: `MapRenderer`/`HudOverlay` (cần vẽ trực tiếp trên board
thật để verify), và chưa wire vào `src/main.cpp` nào — xem
`offline_map/README.md` mục "What's NOT here" cho danh sách đầy đủ phần còn
thiếu và lý do.

## Giới hạn v1 (có thể mở rộng sau)

- Chưa hiển thị tên đường/hướng dẫn Google Maps có dấu tiếng Việt — cần build
  font riêng bằng `lv_font_conv` giống board S3 (`lv_font_vi_14/20`). Toàn bộ
  dữ liệu VMSX (tốc độ, biển báo, khoảng cách, thời tiết) đều là số/ASCII nên
  dùng font Montserrat có sẵn của LVGL là đủ.
- Cảm ứng đã wire vào LVGL nhưng UI hiện chưa có widget tương tác nào (thuần
  hiển thị, giống board S3).
- Chưa dùng khe TF/thẻ SD, camera OV2640, LED RGB — nằm ngoài phạm vi HUD
  hiện tại, chỉ khai báo sẵn pin trong `board_pins.h` cho tương lai.
