# Patch VIETMAP Live 3.3.4 (`178555549`)

Bộ này lưu phần thay đổi tối thiểu, không lưu APK hay toàn bộ cây apktool.

## Input đã kiểm chứng

- APK gốc: `VIETMAP_LIVE_3.3.4+178555549.apk`
- SHA-256: `c5d2de4ab731ff99e0bd6a3b4f007695a9a4e99e58fc7d367c41d3b1e35ff7fc`
- Apktool đã dùng: `3.0.3`

## Nội dung patch

- `hooks.patch`: bốn hook vào host Smali:
  - `Ma/j.C([B)V`: đăng ký GATT/characteristic `FFFF/9ABC`.
  - `Ma/j$a.onCharacteristicWrite`: tiếp tục relay sau callback write.
  - `Ma/j.u()`: giữ kết nối khi relay còn state/GATT hợp lệ **và** đang trong cửa sổ ân hạn 15 giây kể từ lần `registerGatt` với một `BluetoothGatt` mới (`VmslRelay.connectSinceMs`, xem `shouldKeepConnection()`). Sau 15 giây, `u()` chạy lại bình thường để app có thể tự dọn state/GATT cũ và rescan khi mất kết nối thật. Cửa sổ 15 giây chỉ đủ để nuốt lỗi disconnect giả `reason=531` (~10 giây sau connect), không chặn disconnect thật vô thời hạn như bản gốc.
  - `pa/f.q(JSONObject)`: lấy `VMSX` từ Android Auto theo JSON key.
  - `MainActivity`: cập nhật `VMSL` phone-only khi speed/limit thay đổi.
- Helper template: `analysis/apktool-3.3.4/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali`.
- `cluster-padding.patch` (tùy chọn, tách riêng khỏi `hooks.patch` vì phải đo/tune theo từng màn hình): hook `Ta/b.g(Landroid/graphics/Rect;)V`. Class `Ta/b` implement `androidx.car.app.SurfaceCallback`; `g()` là `onVisibleAreaChanged(Rect)` do head unit Android Auto gọi mỗi khi vùng hiển thị an toàn thay đổi. Rect này quyết định cả `EdgeInsets` truyền cho Mapbox camera lẫn Rect truyền cho layer widget HUD (`Va/f.e(Rect)` — nơi vẽ biển giới hạn tốc độ/tốc độ hiện tại/cảnh báo). Patch chỉ co Rect này lại đều 50px mỗi cạnh (top/left/right/bottom, hằng số `0x32` trong patch) **trước khi** lưu/dùng, để toàn bộ UI chính tự né vùng bị che — không cần sửa gì trong `Ta/b$a`/`Ta/b$b`/`Ta/b$c` hay logic vẽ.
  - Vì sao cần: `androidx.car.app` để đúng head unit tự báo `visibleArea` qua callback này; nếu ROM head unit hiện tại (đầu Android Auto gắn taplo) không báo đúng vùng bị các component khác che, VietMap không có cách nào tự biết để né — patch này ép thêm padding thủ công bất kể head unit báo gì.
  - 50px hiện tại đo theo px của `SurfaceContainer.getWidth()/getHeight()` (không phải px vật lý màn hình) — đây vẫn là giá trị khởi điểm đồng nhất 4 cạnh, **chưa đo trên taplo thật**. Nếu vùng bị che thực tế lệch nhiều giữa các cạnh (vd. thanh cảnh báo chỉ che đáy), đo lại rồi sửa từng hằng số `const/16 v15, 0x32` tương ứng trong patch.
  - **ĐÃ GỠ khỏi cây build hiện tại** (`analysis/apktool-3.3.4-full`) vì gây lệch khung hình trong luồng self-host lấy bitmap gửi ESP32 (xem `esp32/android_car_nav/carhost/`): khung capture ở đó chỉ 480x270, 50px/cạnh chiếm tới ~10-18% mỗi chiều, làm nội dung map bị co lại bất thường. Patch file này giữ lại làm tham khảo cho trường hợp gốc (đầu Android Auto/taplo thật) — muốn dùng lại thì áp `cluster-padding.patch` vào bản build riêng cho đầu đó, không áp chung với cây dùng cho self-host.

Tên class/package bị obfuscate có thể đổi ở bản mới. Trước khi apply phải tìm lại anchor theo method signature/API call thay vì chỉ dựa vào số dòng.

## Port sang bản mới

Ví dụ dùng biến `TREE` để tránh hard-code phiên bản:

```sh
TREE="analysis/apktool-NEW"
apktool d -f -o "$TREE" "VIETMAP_LIVE_NEW.apk"
mkdir -p "$TREE/smali_classes2/vn/vietmap/live/patch"
cp analysis/apktool-3.3.4/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali \
  "$TREE/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali"
```

Nếu obfuscation/anchor vẫn giống 3.3.4:

```sh
patch --dry-run -p1 -d "$TREE" < patches/vietmap-live-3.3.4/hooks.patch
patch -p1 -d "$TREE" < patches/vietmap-live-3.3.4/hooks.patch

# Tùy chọn: padding cho vùng hiển thị khi chiếu ra màn cụm đồng hồ (taplo)
patch --dry-run -p1 -d "$TREE" < patches/vietmap-live-3.3.4/cluster-padding.patch
patch -p1 -d "$TREE" < patches/vietmap-live-3.3.4/cluster-padding.patch
```

Nếu dry-run fail, port thủ công đúng năm hook nêu trên rồi kiểm tra register count/label trong từng method (`cluster-padding.patch` chỉ còn một anchor duy nhất: đầu `Ta/b.g(Landroid/graphics/Rect;)V` — có thể đổi tên class/method obfuscate ở bản mới, tìm lại theo interface `androidx.car.app.SurfaceCallback` + tham số `Landroid/graphics/Rect;` + chuỗi log `"visibleArea"`).

## Build và ký local

```sh
apktool b "$TREE" -o analysis/vietmap-live-patched-unsigned.apk

"$ANDROID_HOME/build-tools/36.0.0/zipalign" -P 16 -f 4 \
  analysis/vietmap-live-patched-unsigned.apk \
  analysis/vietmap-live-patched-aligned.apk

"$ANDROID_HOME/build-tools/36.0.0/apksigner" sign \
  --ks "$VIETMAP_KEYSTORE" \
  --ks-key-alias "$VIETMAP_KEY_ALIAS" \
  --out analysis/vietmap-live-patched-signed.apk \
  analysis/vietmap-live-patched-aligned.apk
```

Không đưa keystore/password hoặc APK output vào Git. Dùng đúng signing key cũ mới có thể cài đè bản patched trước mà không xóa app data.

## Validation bắt buộc

1. `apktool b` thành công; `zipalign -P 16 -c 4` và `apksigner verify --verbose --print-certs` đều pass.
2. Cài bằng `adb install -r --no-incremental`; kiểm tra version và không có `FATAL EXCEPTION`.
3. Không Android Auto: serial nhận `VMSL`, current speed đổi dù limit giữ nguyên.
4. Có Android Auto: serial nhận `VMSX`; kiểm tra limit/speed/min/navigationState/flags/checksum.
5. Giữ kết nối quá timeout cũ (~10 giây), không còn disconnect reason `531`.
6. Tắt ESP32 (hoặc để ngoài tầm BLE) sau khi đã kết nối và chờ hơn 15 giây: app phải tự dọn state (`hudConnected=false`/gatt cũ) và quay lại scan/reconnect được khi ESP32 bật lại, thay vì kẹt vĩnh viễn ở trạng thái "tưởng vẫn đang kết nối".
