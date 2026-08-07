# esp32MVN — Trợ lý giọng nói ESP32-S3 + Gemini Live

Firmware ESP-IDF cho board ESP32-S3 có màn hình LCD 1.54" (ST7789), codec âm
thanh ES8311 (mic + loa), nút bấm và cấu hình WiFi qua captive portal. Sau khi
cấu hình WiFi + Gemini API key, nhấn nút để nói chuyện trực tiếp (voice-to-voice)
với Gemini Live API của Google qua WebSocket.

Board tham chiếu: **LC-S3-WiFi-1.54TFT** (pinout lấy từ nhánh
`feature/add-lc-s3-154-board` của [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)).

## Tính năng

- Cấu hình WiFi lần đầu qua SoftAP + captive portal (không cần cài app điện thoại)
- Nhập Gemini API key ngay trên trang cấu hình WiFi
- Hội thoại giọng nói hai chiều với Gemini Live API (server tự phát hiện giọng nói,
  hỗ trợ ngắt lời AI giữa câu)
- Hiển thị transcript: câu người dùng nói (STT) và câu Gemini trả lời, kèm icon
  trạng thái (đang nghe / đang xử lý / đang trả lời) và font tiếng Việt có dấu
- Nút tăng/giảm âm lượng (bấm = ±10%, giữ lâu = max/mute)
- Theo dõi pin (ADC hiệu chuẩn theo board) + trạng thái sạc, LED báo trạng thái
- Tự động bật lại hotspot cấu hình nếu mất kết nối WiFi sau một thời gian retry

## Phần cứng

| Thành phần | Chip/driver | Giao tiếp |
|---|---|---|
| MCU | ESP32-S3 | — |
| LCD 1.54" 240x240 | ST7789 | SPI (SPI3_HOST) |
| Codec âm thanh (mic + loa) | ES8311 | I2C (điều khiển) + I2S full-duplex (dữ liệu) |
| Đo pin | ADC1 CH8 | GPIO9 |
| Nút | Talk (bấm 1 lần để bật/tắt hội thoại), Vol+, Vol- | GPIO |
| LED trạng thái | 1 LED đơn | GPIO |

Toàn bộ số GPIO cụ thể nằm tập trung ở [`main/board_config.h`](main/board_config.h) —
đây là **nơi duy nhất** cần sửa nếu board thật của bạn khác board tham chiếu.

## Cấu trúc project

```
main/
  app_main.c              # entry point + máy trạng thái chính
  board_config.h           # TẤT CẢ định nghĩa GPIO/board
  nvs_settings.c/.h        # lưu WiFi creds, Gemini API key, volume trong NVS
  wifi/                    # SoftAP + captive portal (dns_server, captive_http) + STA/retry
  audio/                   # bring-up ES8311 (esp_codec_dev) + pipeline mic/loa <-> Gemini
  display/                 # LCD ST7789 + LVGL + font tiếng Việt (display/fonts/)
  input/                   # xử lý nút bấm (debounce, click/long-press)
  power/                   # đo pin (ADC) + LED trạng thái
  gemini/                  # WebSocket client cho Gemini Live API
```

## Yêu cầu

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.1 trở lên (đã build/test với v5.5.5)
- Board ESP32-S3 (khuyến nghị có PSRAM, vd module N16R8) — cần cho buffer JSON/audio của Gemini Live

## Build & Flash

```bash
. $HOME/esp/esp-idf/export.sh   # hoặc duong dan ESP-IDF ban da cai
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor   # doi dung cong USB, hoac bo -p de idf.py tu do
```

## Lần đầu sử dụng

1. Cấp nguồn cho board. Nếu chưa từng cấu hình WiFi, board sẽ phát một SoftAP tên
   `ESP32-Setup-XXXX` (XXXX theo MAC, mỗi board khác nhau), mật khẩu mặc định
   `12345678` — cả hai đều hiển thị luôn trên màn hình LCD.
2. Kết nối điện thoại/laptop vào SoftAP đó, mở trình duyệt bất kỳ trang nào để
   captive portal tự bật popup cấu hình (hoặc vào thủ công `http://192.168.4.1`).
3. Nhập SSID/mật khẩu WiFi thật + Gemini API key, bấm lưu. Board tự khởi động lại
   và kết nối WiFi.
4. Khi màn hình hiện "Sẵn sàng", bấm nút Talk để bắt đầu nói chuyện với Gemini;
   bấm lại để kết thúc phiên.

Nếu board rớt WiFi và không tự kết nối lại được sau vài lần thử, nó sẽ tự bật lại
hotspot cấu hình và màn hình sẽ ghi rõ "Mất kết nối WiFi" để bạn biết cần cài lại.

## Trước khi dùng với board/API thật — cần kiểm tra lại

- **`main/board_config.h`**: đối chiếu với schematic board thật nếu không dùng
  đúng board LC-S3-WiFi-1.54TFT.
- **`main/gemini/gemini_config.h`**: model id và cấu trúc JSON của Gemini Live API
  thay đổi khá thường xuyên — xác minh lại với
  [ai.google.dev/api/live](https://ai.google.dev/api/live) trước khi flash thật.
- API key nhập qua captive portal được lưu thẳng trong NVS (không mã hoá) — chấp
  nhận được cho dùng cá nhân, nhưng không phải giải pháp bảo mật cấp sản xuất.

## Môi trường phát triển đã build thử

Firmware đã build thành công (0 lỗi, 0 warning) với ESP-IDF v5.5.5, target esp32s3,
qua các managed component: `espressif/esp_codec_dev`, `espressif/esp_lvgl_port`,
`lvgl/lvgl`, `espressif/esp_websocket_client`. Chưa được kiểm thử trên phần cứng
thật (chưa flash lên board) — cần người dùng tự flash và phản hồi lại nếu có lỗi
runtime.
