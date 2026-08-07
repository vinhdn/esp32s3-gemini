# Build project trên Windows 11

Project này build/test chính trên macOS (ESP-IDF v5.5.5), nhưng ESP-IDF vốn
cross-platform nên build trên Windows hoàt toàn được — chỉ cần chú ý vài điểm
đặc thù của Windows dưới đây (chủ yếu là **đường dẫn** và **cổng COM**).

## 1. Cài ESP-IDF

Cách đơn giản nhất: dùng **ESP-IDF Windows Installer** (khuyến nghị, đặc biệt
nếu bạn chưa quen dòng lệnh):

1. Tải installer tại trang chính thức Espressif:
   `https://dl.espressif.com/dl/esp-idf/` → chọn bản Windows Installer.
2. Chạy installer, khi được hỏi chọn **ESP-IDF version**: chọn `v5.1` trở lên
   (project này đã build/test với `v5.5.5`).
3. **Quan trọng — đường dẫn cài đặt**: giữ đường dẫn ngắn, không dấu cách,
   không ký tự có dấu, ví dụ `C:\esp\esp-idf` hoặc mặc định `C:\Espressif`.
   Windows giới hạn đường dẫn 260 ký tự theo mặc định; cấu trúc build của
   ESP-IDF (`managed_components\...\CMakeFiles\...`) lồng khá sâu nên dễ vượt
   giới hạn này nếu cài ở đường dẫn dài (vd trong `Desktop` hay `OneDrive`).
4. Installer tự cài Python, Git, toolchain xtensa, và tạo shortcut trong Start
   Menu tên **"ESP-IDF 5.x CMD"** (và/hoặc PowerShell) — mở shortcut này để có
   sẵn môi trường `idf.py`, không cần tự "source" gì thêm.

Nếu muốn cài thủ công bằng Git (giống cách làm trên macOS/Linux):

```powershell
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git C:\esp\esp-idf
cd C:\esp\esp-idf
.\install.bat esp32s3
.\export.ps1
```

(`export.ps1` chỉ có hiệu lực trong phiên PowerShell hiện tại — mỗi terminal
mới đều cần chạy lại, hoặc dùng shortcut Start Menu ở trên.)

## 2. Copy project vào đường dẫn ngắn

Copy toàn bộ thư mục `esp32MVN` vào một đường dẫn ngắn, ví dụ `C:\esp\esp32MVN`.

- **Tránh để project trong OneDrive** (hoặc bất kỳ thư mục đang đồng bộ cloud
  nào) — OneDrive có thể lock file đang build, và path trong OneDrive
  (`C:\Users\<ten>\OneDrive\...`) thường đã dài sẵn, cộng thêm path build của
  ESP-IDF rất dễ vượt 260 ký tự.
- Nếu không tránh được path dài: bật **Win32 long path** — `gpedit.msc` →
  Computer Configuration → Administrative Templates → System → Filesystem →
  "Enable Win32 long paths" → Enabled (cần khởi động lại máy).

## 3. Build & flash

Mở terminal **"ESP-IDF CMD"** (hoặc PowerShell đã `export.ps1`), rồi:

```powershell
cd C:\esp\esp32MVN
idf.py set-target esp32s3
idf.py build
```

Cắm board qua USB, mở **Device Manager → Ports (COM & LPT)** để xem board
được gán cổng nào (ví dụ `COM5`), rồi:

```powershell
idf.py -p COM5 flash monitor
```

(Bỏ `-p COM5` để `idf.py` tự dò cổng, nhưng chỉ định rõ thường nhanh và chắc
hơn nếu máy có nhiều thiết bị serial khác.)

## Lỗi thường gặp trên Windows

| Triệu chứng | Nguyên nhân / cách sửa |
|---|---|
| Build lỗi `File name too long`, hoặc CMake báo path error | Đường dẫn project hoặc ESP-IDF quá dài/sâu — xem lại mục 1-2, đổi sang path ngắn hoặc bật long path. |
| Không thấy cổng COM nào trong Device Manager | Thiếu driver USB-to-serial cho chip trên board (thường là CP210x của Silicon Labs hoặc CH340) — cài driver tương ứng. Thử cáp USB khác nếu cáp chỉ hỗ trợ sạc (không có dây data). |
| `idf.py` / `python` không được nhận diện (`not recognized as a command`) | Đang mở terminal thường (không phải "ESP-IDF CMD") và chưa chạy `export.ps1`/`export.bat`. Dùng shortcut Start Menu, hoặc tự chạy export trong đúng terminal đang dùng. |
| Flash báo lỗi timeout / `Failed to connect` | Một số board ESP32-S3 cần giữ nút BOOT khi cấp nguồn/khi bắt đầu flash để vào chế độ nạp — thử giữ nút BOOT, cắm lại USB, chạy lại lệnh flash. |

## Không cần cài Node.js/npm trên Windows

Hai font tiếng Việt trong `main/display/fonts/` đã được generate sẵn thành mã
nguồn C (`font_vi_14.c`, `font_vi_20.c`) và commit vào project — bạn **không
cần** cài Node.js/npm hay công cụ `lv_font_conv` để build bình thường. Công cụ
đó chỉ cần lại nếu muốn tạo font mới hoặc đổi cỡ chữ.
