# Phân tích VIETMAP Live 3.2.6 và `secrect.bin`

## Kết luận

1. `secrect.bin` **không phải firmware HUD/ESP32**. Đây là container dữ liệu bản đồ/cảnh báo gồm `edogen.bin`, `citiesen.bin`, `districtsen.bin` và `roadsenz.bin`. Vì vậy không thể lấy UUID, handshake, command ID, AES key hay frame HUD từ file này.
2. APK là Flutter release AOT. Logic H50 nằm trong `lib/*/libapp.so`, không nằm trong `VMBluetoothService`.
3. APK có các dấu vết xác nhận luồng H50: `H50Protocol`, `H50ProtocolCmd`, `sendCurrentSpeed`, `sendSpeedLimit`, `sendBytesData`, `AESMode`, `ENCRYPTED_SIZE`, `_splitPacketForMtuByte`, `Sent encrypted speed limit:` và `Encryption failed:`.
4. Android FlutterBluePlus chỉ nhận byte array đã được Dart đóng gói/mã hóa, sau đó gọi `BluetoothGatt.writeCharacteristic`. Smali không chứa plaintext speed limit tại điểm GATT write.
5. Có một đường plaintext riêng dành cho Android Auto: model native `Laa/d` chứa hai trường `speedLimit` và `speed`; `LZ9/f.q(JSONObject)` parse hai trường này. Đây là điểm hook/patch dễ, nhưng cần kiểm tra runtime vì có thể chỉ được cập nhật khi Android Auto đang hoạt động.
6. Chưa thể khẳng định byte-level packet format, command ID, key/IV/mode hoặc checksum chỉ từ hai file đã cung cấp. Cần ít nhất một capture runtime từ H50/ESP32 với các speed limit đã biết.

## Artifact và hash

- APK: `VIETMAP_LIVE_3.2.6+177424880.apk`
  - Package: `vn.vietmap.live`
  - Version: `3.2.6` (`177424880`)
  - SHA-256: `2750ce3b8402bd0a37bb4739142cfb1587f8b987bb451f49967025851d8b7619`
  - Chữ ký gốc: APK Signature Scheme v2/v3.
- Data container: `secrect.bin`
  - SHA-256: `c3210a65bf0e766a9dcb49b20ee07ab3abf507f238885e6a6122f48da2391cb1`
  - Kích thước: `105568153` byte.

APK đã được decompile tại `analysis/apktool/`.

## Dấu vết code quan trọng

- `analysis/apktool/smali/i6/g.smali`
  - FlutterBluePlus Android plugin.
  - API 33+: gọi `BluetoothGatt.writeCharacteristic(characteristic, byte[], writeType)` quanh dòng 7379.
  - API cũ: `characteristic.setValue(byte[])`, sau đó `BluetoothGatt.writeCharacteristic(characteristic)` quanh dòng 7431.
- `analysis/apktool/smali/i6/g$d.smali`
  - Concrete `BluetoothGattCallback`.
  - `onCharacteristicChanged(...)` chuyển dữ liệu notify thành `OnCharacteristicReceived`.
  - `onCharacteristicWrite(...)` trả `OnCharacteristicWritten`, bao gồm service UUID, characteristic UUID và value.
- `analysis/apktool/smali_classes2/vn/vietmap/live/VMBluetoothService.smali`
  - Chỉ quản lý foreground notification và widget/overlay; không phải H50 encoder.
- `analysis/apktool/smali_classes2/aa.1/d.smali`
  - Native navigation-state model có `speedLimit`, `speed`, `laneSpeedLimits`, `minSpeedLimit`.
  - `G()` serialize các trường sang JSON.
- `analysis/apktool/smali_classes2/Z9/f.smali`
  - `q(JSONObject)` parse `speedLimit` và `speed` thành `Integer` trước khi render Android Auto.
- `analysis/apktool/lib/armeabi-v7a/libapp.so`
  - Slice thực tế phù hợp thiết bị MA8000 (`armeabi-v7a`). Chứa Dart AOT H50 protocol.

## Luồng dữ liệu đã xác nhận

```text
Navigation state (Dart)
  ├─ plaintext speedLimit/current speed
  ├─ Android Auto bridge -> JSONObject -> Z9/f.q(...)  [plaintext, có thể có điều kiện]
  └─ H50Protocol.sendSpeedLimit(...)
       -> protocol framing/encryption trong Dart AOT
       -> split theo MTU nếu cần
       -> FlutterBluePlus MethodChannel
       -> i6/g.smali
       -> BluetoothGatt.writeCharacteristic(...)       [ciphertext/frame]
       -> VIETMAP_HUD_H50
```

HUD trả notify/indicate theo chiều ngược lại qua `i6/g$d.onCharacteristicChanged` rồi thành `HUDResponse.receiveBytesData` trong Dart.

## Bộ capture runtime

- `analysis/capture_vietmap_h50.js`: Frida hooks GATT TX/RX, MTU và Android Auto plaintext state.
- `analysis/capture_vietmap_h50.py`: runner có workaround cho Frida 17.17 trên Python 3.10.

Chạy khi MA8000 được nối USB lại:

```sh
python3 analysis/capture_vietmap_h50.py \
  --device raman801df76c07c02d10 \
  --spawn | tee analysis/h50-capture.jsonl
```

Nên tạo chuỗi test có nhãn thời gian:

```text
speed limit: 30 -> 40 -> 50 -> 60 -> 80 -> 100 -> 120
current speed: giữ cố định nếu có thể
mỗi giá trị giữ 10-15 giây và lặp lại hai lần
```

Record cần quan tâm:

- `plaintext_navigation_state`: ground truth `speed_limit` và `current_speed` nếu Android Auto bridge active.
- `gatt_tx`: service UUID, characteristic UUID, write type và packet app gửi.
- `gatt_rx`: packet notify/response từ HUD.
- `gatt_mtu`: MTU negotiated để ghép chunk đúng.

Nếu không xuất hiện `plaintext_navigation_state`, vẫn dùng timestamp/video màn hình để ghép speed limit hiển thị với `gatt_tx`.

## Cách suy ra protocol sau capture

1. Nhóm packet theo service/characteristic UUID và chiều TX/RX.
2. Loại packet current speed bằng cách thay current speed trong khi giữ speed limit cố định.
3. So sánh packet của cùng một speed limit:
   - Giống hoàn toàn: có thể dùng key/IV cố định, ECB hoặc obfuscation/frame đơn giản.
   - Khác nhau: tìm nonce/counter/IV và phần header ổn định.
4. Kiểm tra độ dài theo block 16 byte để đánh giá AES; không kết luận AES chỉ dựa trên chuỗi `AESMode`.
5. Đối chiếu response/ACK sau mỗi write để xác định command và sequence.
6. Khi có plaintext/ciphertext pairs, thử key derivation từ serial HUD; chuỗi `LONGSERIAL1234567890ABCD` có trong AOT nhưng hiện chỉ là manh mối, chưa phải key đã xác nhận.

## Lựa chọn triển khai

### Khuyến nghị: ESP32 giả lập H50, không sửa APK

Cho ESP32 advertise tên `VIETMAP_HUD_H50`, service/characteristic và properties đúng theo capture. ESP32 nhận packet, ghép chunk theo MTU và parse/decrypt command speed limit. Cách này không làm mất chữ ký/app data và ít hỏng khi VietMap cập nhật.

### Patch APK qua native Android Auto plaintext

Có thể thêm code tại `LZ9/f;->q(Lorg/json/JSONObject;)V` ngay sau khi đọc `speedLimit`, rồi gửi giá trị sang ESP32 bằng BLE/UDP/Broadcast. Ưu điểm là không cần giải mã H50; nhược điểm là cần xác nhận method này chạy khi không có Android Auto.

### Patch trực tiếp `H50Protocol.sendSpeedLimit`

Đây là điểm luôn đúng về mặt logic nhưng nằm trong Dart AOT. Cần source `vietmap_hud_sdk` hoặc công cụ AOT reverse phù hợp (Blutter/Darter/Ghidra setup). Patch máy ARMv7 trực tiếp không nên làm trước khi có function boundary/call graph chính xác.

## Rebuild và resign

Chỉ thực hiện sau khi chốt patch. Quy trình local dự kiến:

```sh
apktool b analysis/apktool -o analysis/vietmap-live-patched-unsigned.apk

/Users/vinhdo/Library/Android/sdk/build-tools/36.0.0/zipalign \
  -P 16 -f -v 4 \
  analysis/vietmap-live-patched-unsigned.apk \
  analysis/vietmap-live-patched-aligned.apk

/Users/vinhdo/Library/Android/sdk/build-tools/36.0.0/apksigner sign \
  --ks analysis/vietmap-debug.jks \
  --out analysis/vietmap-live-patched-signed.apk \
  analysis/vietmap-live-patched-aligned.apk

/Users/vinhdo/Library/Android/sdk/build-tools/36.0.0/apksigner verify \
  --verbose --print-certs \
  analysis/vietmap-live-patched-signed.apk
```

Không tạo keystore hoặc ký APK ở giai đoạn phân tích này.

### Lưu ý chữ ký

Key gốc không có trong APK. APK ký bằng key mới không thể cài đè bản VietMap chính thức. Gỡ bản chính thức trước khi cài sẽ xóa app data; server/integrity check cũng có thể từ chối bản sửa. Trên thiết bị root có thể dùng môi trường test riêng, nhưng không nên thay bản đang sử dụng trước khi capture xong.

## Trạng thái còn thiếu

Để chốt decoder ESP32 cần một trong ba đầu vào:

1. MA8000 nối USB lại và board/HUD ở trong tầm để chạy capture;
2. file HCI snoop (`btsnoop_hci.log`) của một phiên kết nối;
3. log ESP32 gồm timestamp + raw packet hex cho các speed limit biết trước.

Firmware HUD thực (dump flash `.bin` của ESP32/MCU) cũng hữu ích, nhưng `secrect.bin` hiện tại không phải file đó.

## Cập nhật kiểm chứng runtime trên Pixel 3 XL và ESP32 (2026-08-26)

Đã hook trực tiếp process `vn.vietmap.live` đang cài trên Pixel 3 XL (`8A4Y0F6M5`) trong lúc Android Auto hoạt động và ESP32 nối USB tại `/dev/cu.usbmodem1301`.

Thông số BLE đã xác nhận bằng runtime:

- Tên thiết bị: `VIETMAP_HUD_H50`
- Địa chỉ BLE: `1C:DB:D4:AB:A7:D2`
- Service: `0000ffff-0000-1000-8000-00805f9b34fb`
- Write characteristic: `00009abc-0000-1000-8000-00805f9b34fb`
- Write type: `2`
- MTU do ESP32 báo: `256`

Đường `LZ9/f.q(JSONObject)` liên tục cung cấp plaintext thật: `speedLimit=50`, `speed=0..2`, trong khi `hudConnected=false`. App gửi các frame H50 16 byte thay đổi giữa các phiên; vì ESP32 không notify/ACK và app không chuyển sang `hudConnected=true`, các frame này chỉ nên xem là handshake/get-info candidate, chưa phải bằng chứng về frame speed limit.

Đã kiểm chứng relay tùy biến trên cùng characteristic bằng frame 8 byte:

```text
offset  size  nội dung
0       4     ASCII "VMSL" (56 4d 53 4c)
4       1     version = 01
5       1     speedLimit
6       1     currentSpeed
7       1     XOR của byte 0..6
```

Ví dụ đã được Android BLE stack chấp nhận:

- limit `50`, current `0`: `564d534c01320037`
- limit `50`, current `1`: `564d534c01320136`
- limit `50`, current `2`: `564d534c01320235`

Sau khi thêm guard `relayWriteActive`, capture 32 giây ghi nhận đúng hai `speed_limit_relay` cho hai phiên kết nối BLE, không còn relay tự hook lại mỗi khoảng 700 ms:

1. `564d534c01320136`, `accepted=true`
2. `564d534c01320037`, `accepted=true`

Các cập nhật plaintext lặp lại và thay đổi `currentSpeed` không tạo thêm frame; relay chỉ gửi lại khi có GATT mới hoặc `speedLimit` đổi. ESP32 hiện chỉ log connect, MTU và disconnect, chưa log raw characteristic write, nên `accepted=true` chỉ xác nhận Android nhận yêu cầu ghi chứ chưa chứng minh firmware ESP32 đã parse payload.

Bước triển khai phù hợp nhất hiện tại là: patch đường plaintext để phát một frame `VMSL` khi `speedLimit` đổi, đồng thời thêm handler cho characteristic `0x9ABC` trong firmware ESP32 và log các payload bắt đầu bằng `56 4d 53 4c`. Cần source firmware ESP32 để xác nhận end-to-end trước khi ký và thay APK đang cài; `secrect.bin` không thể dùng cho bước này vì đó là dữ liệu bản đồ, không phải firmware.

## Kiểm chứng end-to-end với firmware ESP32 (2026-08-26)

Source firmware được cung cấp tại `esp32/`. Project dùng ESP-IDF 5.5.5, target ESP32-S3. Callback `access_cb()` trong `esp32/main/ble/waze_hud_ble.c` quản lý write characteristic `0x9ABC` của service `0xFFFF`.

Đã thêm nhánh parser `VMSL` với các điều kiện:

- Đúng 8 byte và magic `56 4d 53 4c`.
- Version tại byte 4 bằng `1`.
- Byte 7 bằng XOR của byte 0..6.
- Frame hợp lệ gọi callback hiện có theo thứ tự `currentSpeed, speedLimit`, từ đó cập nhật UI qua `on_car_data()`/`ui_car_update()`.
- `VMSL` không được echo sang notify `0x1234`, tránh để VietMap nhầm với response handshake H50.
- Các frame binary proprietary khác vẫn giữ đường xử lý H50 cũ.

Firmware build thành công; artifact `esp32/build/esp32mvn.bin` có kích thước `0x10d600`, còn 65% app partition. Đã flash và verify hash thành công qua `/dev/cu.usbmodem1301` lên ESP32-S3 rev 0.2, PSRAM 8 MB.

Capture sau khi restart riêng process VietMap (không xóa app data):

```text
Pixel plaintext: speedLimit=50, currentSpeed=0
Pixel GATT TX:    564d534c01320037, accepted=true
ESP32 serial:     VMSL hop le: speed_limit=50 current_speed=0 checksum=0x37
```

Kết quả này chứng minh đường dữ liệu hoàn chỉnh `VietMap plaintext -> GATT 0x9ABC -> parser ESP32 -> callback UI`. Không cần giải mã AES/protocol proprietary H50 để lấy giới hạn tốc độ. Bước tiếp theo là đưa logic relay hiện đang chạy bằng Frida vào APK: cập nhật state tại `Z9/f.q(JSONObject)`, giữ GATT/characteristic H50 tại FlutterBluePlus, và chỉ gửi `VMSL` sau write hoàn tất hoặc khi speed limit đổi. APK ký key mới chỉ nên tạo làm artifact trước; không gỡ bản chính thức trên Pixel nếu chưa sao lưu app data.

## APK final đã patch, ký và kiểm chứng trực tiếp

Relay đã được đưa vào APK decompile theo đường runtime thực tế:

1. `analysis/apktool/smali_classes2/Z9/f.smali`: sau khi parse `speedLimit` và `speed` plaintext từ Android Auto, gọi `VmslRelay.updateState(Integer, Integer)`.
2. `analysis/apktool/smali_classes2/wa/j.smali`: tại `Lwa/j;->F([B)V`, đăng ký đúng `BluetoothGatt` và write characteristic ngay trước lệnh ghi proprietary H50.
3. `analysis/apktool/smali_classes2/wa/j$a.smali`: tại `onCharacteristicWrite(...)`, chuyển callback qua relay; chỉ callback synthetic của frame `VMSL` bị consume, callback proprietary gốc vẫn chạy.
4. `analysis/apktool/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali`: giữ state/GATT, lọc UUID `FFFF/9ABC`, gửi write type `2`, tạo checksum XOR, chỉ gửi khi GATT mới hoặc `speedLimit` đổi và dùng identity guard chống recursion.

Các hook tương thích trong `i6/g.smali`/`i6/g$d.smali` vẫn được giữ, nhưng capture runtime chứng minh H50 trên thiết bị này đi qua `wa/j`/`wa/j$a`, không đi qua callback FlutterBluePlus `i6/g$d`. Hai log chẩn đoán `state limit=...` và `onCharacteristicWrite hook` đã bị xóa khỏi bản final.

### Artifact final

- APK: `analysis/vietmap-live-vmsl-final-signed.apk`
- Package/version: `vn.vietmap.live` / `3.2.6` (`177424880`)
- Kích thước: `391294502` byte
- SHA-256 APK: `6c326c6ba63dfc350d2886f5df8b859b239bcf090133e243a61966e73164252c`
- Keystore local: `analysis/vietmap-vmsl-test.jks` (file được ignore, không commit)
- Alias/password: giữ trong cấu hình local, không ghi vào repository
- Certificate SHA-256: `25cd6843aa6b36dddbfb5675f111babaa7dfc927299509ed76c2bd067d197435`

Kiểm chứng artifact:

- Apktool 3.0.3 build thành công cả `classes.dex`, `classes2.dex`, resources, assets và native libs.
- `zipalign -P 16 -c 4` thành công.
- APK Signature Scheme v2 và v3 đều verify thành công; một signer RSA 2048 bit.
- `apkanalyzer` xác nhận package `vn.vietmap.live`, version `3.2.6`, versionCode `177424880`.

### Cài đè và smoke-test final trên Pixel

Đã cài đè bằng cùng test key, không uninstall:

```sh
adb -s 8A4Y0F6M5 install -r --no-incremental \
  analysis/vietmap-live-vmsl-final-signed.apk
```

Lệnh trả về `Success`. `firstInstallTime` vẫn là `2026-08-26 17:20:34`, còn `lastUpdateTime` đổi thành `2026-08-27 08:44:37`, xác nhận đây là update tại chỗ và app data không bị xóa. Các quyền BLE/location trước đó vẫn được cấp.

Sau khi force-stop và mở lại VietMap trong lúc Android Auto đang bound, ESP32 ghi nhận:

```text
2026-08-27T01:45:05.247094+00:00  BLE connected
2026-08-27T01:45:06.623720+00:00  MTU=256
2026-08-27T01:45:07.695426+00:00  proprietary H50 write, 16 byte
2026-08-27T01:45:07.725798+00:00  VMSL hop le: speed_limit=50 current_speed=0 checksum=0x37
```

Trong capture 45 giây chỉ có một frame `VMSL`; payload tương ứng là `564d534c01320037`. Logcat không có `FATAL EXCEPTION`, không có crash của `vn.vietmap.live` và không còn output mang tag/message diagnostic `VmslRelay`.

Kết quả cuối chứng minh không cần Frida và không cần giải mã AES H50: APK final tự lấy plaintext Android Auto, gửi frame qua GATT `0x9ABC`, ESP32 validate checksum rồi chuyển dữ liệu vào callback UI. Phải giữ an toàn `analysis/vietmap-vmsl-test.jks`; mọi APK cập nhật sau này cần ký bằng đúng key này để tiếp tục cài đè mà không xóa dữ liệu.

## Nâng cấp patch lên VIETMAP Live 3.3.3

APK gốc mới:

- File: `VIETMAP_LIVE_3.3.3+178453857.apk`
- Package/version: `vn.vietmap.live` / `3.3.3` (`178453857`)
- Kích thước: `351523812` byte
- SHA-256: `5365b441afe72317d971acf740eec7aa2dfa941b15460fe9d0d81df5d173dfa1`
- Certificate gốc SHA-256: `684cedb49ccaca3e114de682417001b14e9d945d6f73cf05b25ca354d4a8bfee`

Bản 3.3.3 được decompile riêng tại `analysis/apktool-3.3.3/`, không ghi đè cây 3.2.6. Obfuscation thay đổi nhưng cấu trúc logic vẫn tương đương:

- Parser Android Auto: `LZ9/f;->q(JSONObject)` đổi thành `Lpa/f;->q(JSONObject)`. Do collision tên package không phân biệt hoa/thường trên macOS, file vật lý là `analysis/apktool-3.3.3/smali_classes2/pa.1/f.smali`.
- Sender H50: `Lwa/j;->F([B)V` đổi thành `LMa/j;->C([B)V` tại `analysis/apktool-3.3.3/smali_classes2/Ma/j.smali`.
- Callback write: `Lwa/j$a;` đổi thành `LMa/j$a;` tại `analysis/apktool-3.3.3/smali_classes2/Ma/j$a.smali`.

Đã port ba hook chức năng:

1. Gọi `VmslRelay.updateState(v11, v12)` ngay sau khi parse `speedLimit` và `speed`.
2. Gọi `VmslRelay.registerGatt(v0, v1)` giữa `BluetoothGattCharacteristic.setValue(...)` và `BluetoothGatt.writeCharacteristic(...)` trong `Ma/j.C([B)V`.
3. Gọi `VmslRelay.onCharacteristicWrite(p1, p2, p3)` ngay sau callback framework và return sớm chỉ với callback synthetic của relay.

Helper final không có log diagnostic được port nguyên vào `analysis/apktool-3.3.3/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali`. Frame và semantics giữ nguyên: `VMSL`, version 1, limit/current byte, XOR checksum; chỉ gửi khi GATT mới hoặc speed limit đổi.

### Artifact 3.3.3 final

- APK: `analysis/vietmap-live-3.3.3-vmsl-final-signed.apk`
- Kích thước: `352430988` byte
- SHA-256: `40b7de88cd429d0cbab8599d4731bde66040d3d1b056f83512e3129576756074`
- Certificate test SHA-256: `25cd6843aa6b36dddbfb5675f111babaa7dfc927299509ed76c2bd067d197435`
- APK Signature Scheme v2/v3: hợp lệ
- `zipalign -P 16 -c 4`: hợp lệ
- Package/version sau build: `vn.vietmap.live` / `3.3.3` (`178453857`)

Đã cài đè lên bản patched 3.2.6 bằng cùng test key:

```sh
adb -s 8A4Y0F6M5 install -r --no-incremental \
  analysis/vietmap-live-3.3.3-vmsl-final-signed.apk
```

Lệnh trả về `Success`. Sau update, `firstInstallTime` vẫn là `2026-08-26 17:20:34`, `lastUpdateTime` là `2026-08-27 09:07:53`; các quyền BLE/location vẫn được cấp. Điều này xác nhận update tại chỗ không uninstall và không xóa app data.

Smoke-test trực tiếp 45 giây, không Frida:

```text
2026-08-27T02:08:31.227228+00:00  BLE connected
2026-08-27T02:08:32.688770+00:00  MTU=256
2026-08-27T02:08:33.873228+00:00  proprietary H50 write, 16 byte
2026-08-27T02:08:33.890487+00:00  VMSL hop le: speed_limit=50 current_speed=0 checksum=0x37
```

ESP32 nhận đúng một frame VMSL `564d534c01320037` trong phiên capture. Android Auto service tự bind lại sau force-stop; logcat không có `FATAL EXCEPTION`, crash của `vn.vietmap.live` hoặc log diagnostic `VmslRelay`. Vì vậy patch 3.3.3 đã được xác nhận end-to-end trên Pixel và firmware ESP32 hiện tại.

## Cập nhật VIETMAP Live 3.3.4 và mở rộng dữ liệu từ Android Auto

APK gốc mới:

- File: `VIETMAP_LIVE_3.3.4+178555549.apk`
- Package/version: `vn.vietmap.live` / `3.3.4` (`178555549`)
- SHA-256: `c5d2de4ab731ff99e0bd6a3b4f007695a9a4e99e58fc7d367c41d3b1e35ff7fc`
- Decompile tại `analysis/apktool-3.3.4/`

Obfuscation không đổi so với 3.3.3, nên toàn bộ anchor được giữ nguyên: `pa.1/f.smali` (parser Android Auto), `Ma/j.C([B)V` (sender H50), `Ma/j$a.onCharacteristicWrite`, `Ma/j.u()` (disconnect) và hai nhánh phone-only trong `MainActivity`.

### Dữ liệu thực sự lấy được từ `VIETMAPLiveAndroidAutoService`

Model overlay `qa/d` (`CarMapOverlayData`) khai báo 38 JSON key. Các trường primitive dùng được ngay cho HUD:

| JSON key | Kiểu | Ý nghĩa |
| --- | --- | --- |
| `speedLimit` | Integer | Giới hạn tốc độ hiện tại, km/h |
| `speed` | Integer | Tốc độ hiện tại, km/h |
| `overSpeed` | Boolean | Đang vượt tốc độ |
| `minSpeedLimit` | Integer | Tốc độ tối thiểu của đoạn đường |
| `underMinSpeedLimit` | Boolean | Đang chạy dưới tốc độ tối thiểu |
| `navigationState` | Integer | Trạng thái dẫn đường |
| `hudConnected` | Boolean | App coi HUD đã kết nối hay chưa |
| `upcomingAlerts[0].distance` | int (mét) | Khoảng cách tới cảnh báo kế tiếp |
| `upcomingAlerts[0].speedLimit` | Integer | Giới hạn tốc độ tại cảnh báo đó |

Các trường còn lại phức tạp hoặc không hữu ích cho HUD nhỏ: `laneInfo`, `laneSpeedLimits`, `tpmsInfos`, `poiOnRoutes`, `restAreaInfo`, `weatherInfo`, `carTheme`, `addressModelView`, ảnh cảnh báo (`imageBase64Data`). Đáng lưu ý: `routeProgress` **không** chứa ETA hay khoảng cách còn lại, chỉ là `progress`/`size` của thanh tiến trình UI, nên không dùng được để hiện thời gian tới đích.

Hook Android Auto giờ nhận trực tiếp `JSONObject` (`VmslRelay.updateFromJson`) và đọc theo tên key, thay vì phụ thuộc class model bị obfuscate. Cách này bền hơn khi VietMap cập nhật.

### Frame mở rộng `VMSX`

Giữ nguyên `VMSL` 8 byte cho đường điện thoại (không Android Auto) và thêm frame 14 byte khi có dữ liệu Android Auto:

```text
offset  size  nội dung
0       4     ASCII "VMSX" (56 4d 53 58)
4       1     version = 01
5       1     speedLimit
6       1     currentSpeed
7       1     flags: bit0 overSpeed, bit1 underMinSpeedLimit,
                     bit2 hudConnected, bit3 cảnh báo phía trước hợp lệ
8       1     minSpeedLimit
9       1     navigationState
10-11   2     khoảng cách tới cảnh báo (uint16 big-endian, mét)
12      1     speedLimit của cảnh báo đó
13      1     XOR của byte 0..12
```

Firmware `esp32/main/ble/waze_hud_ble.c` thêm nhánh parser `VMSX` đặt giữa `VMSL` và nhánh H50 proprietary. Frame hợp lệ được log đầy đủ, gọi `s_data_cb(speed, limit)` như cũ, và đẩy thông tin mở rộng lên UI qua `s_nav_cb`: khoảng cách cảnh báo, giới hạn tại cảnh báo, tốc độ tối thiểu và trạng thái vượt tốc. `VMSX` cũng không echo sang notify `0x1234`.

### Artifact và kiểm chứng

- APK: `analysis/vietmap-live-3.3.4-vmsx-signed.apk`
- SHA-256: `fce961d68a031c45e90de5c580ae8f00c47b9ee002fde1ab89b7a16b7310e539`
- Certificate: `25cd6843aa6b36dddbfb5675f111babaa7dfc927299509ed76c2bd067d197435` (giữ nguyên test key)
- Chữ ký v2/v3 và alignment 16 KiB: hợp lệ
- Firmware: `esp32/build/esp32mvn.bin`, kích thước `0x10d9e0`, còn 65% app partition; đã flash và verify hash qua `/dev/cu.usbmodem1301`

Cài đè giữ dữ liệu: `firstInstallTime` vẫn `2026-08-26 17:20:34`, `lastUpdateTime` `2026-08-27 10:40:50`.

Capture 70 giây khi Android Auto **không** kết nối:

```text
BLE connected, MTU=256
VMSL hop le: speed_limit=50 current_speed=0
VMSL hop le: speed_limit=50 current_speed=45
VMSL hop le: speed_limit=60 current_speed=45
VMSL hop le: speed_limit=60 current_speed=40
VMSL hop le: speed_limit=60 current_speed=1
VMSL hop le: speed_limit=60 current_speed=0
VMSL hop le: speed_limit=50 current_speed=0
```

Kết quả này xác nhận ba điều trên bản 3.3.4: đường phone-only hoạt động không cần Android Auto, tốc độ hiện tại được cập nhật liên tục kể cả khi giới hạn không đổi, và kết nối BLE không còn bị ngắt theo chu kỳ. Nhánh `VMSX` chỉ phát khi `VIETMAPLiveAndroidAutoService` được Android Auto bind, nên cần một phiên có Android Auto để kiểm chứng các trường mở rộng.

### Kiểm chứng `VMSX` với Android Auto đã kết nối

`dumpsys` xác nhận `VIETMAPLiveAndroidAutoService` được `com.google.android.projection.gearhead` bind. Capture 70 giây:

```text
VMSX hop le: limit=50 speed=18 min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=19 min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=1  min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=5  min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=1  min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=0  min=0 nav_state=0 over=0 under_min=0 hud=0
```

Frame mở rộng được parse hợp lệ, checksum đúng, và tốc độ hiện tại cập nhật liên tục dù giới hạn không đổi. Không có `reason=531` trong cửa sổ capture, tức keepalive vẫn giữ được kết nối khi đã có Android Auto.

Ba trường còn bằng 0 trong phiên này là do trạng thái thực tế, không phải lỗi parser:

- `min=0` và `nav_state=0`: chưa dẫn đường theo lộ trình, nên `minSpeedLimit` và `navigationState` chưa có giá trị.
- `hud=0`: app vẫn coi `hudConnected=false` vì ESP32 không trả ACK proprietary H50. Điều này không ảnh hưởng đường VMSL/VMSX.
- Không có dòng `canh bao phia truoc`: `upcomingAlerts` đang rỗng khi không dẫn đường. Cần một phiên chỉ đường có camera/cảnh báo phía trước để thấy khoảng cách và giới hạn tại cảnh báo.

### Thay đổi hiển thị trên ESP32 (240x240)

Trước đây biển giới hạn và tốc độ hiện tại nằm ở góc trên phải với font 20. Đã đổi thành hai vòng số lớn căn giữa và thêm dòng `navigationState` dưới cùng:

- Biển giới hạn: vòng tròn `112x112`, viền đỏ dày 7, số dùng `lv_font_speed_64`, căn giữa lệch trái (`LV_ALIGN_CENTER, -54, -22`).
- Tốc độ hiện tại: vòng tròn `104x104`, viền xanh, số dùng `lv_font_speed_64`, căn giữa lệch phải (`LV_ALIGN_CENTER, 58, -22`). Vẫn đổi sang đỏ khi vượt giới hạn.
- Hai vòng chiếm khoảng y `42..154`, chừa lề 10 px mỗi bên.
- Các dòng navigation dịch xuống dưới: icon hướng rẽ `24x24` tại y `156`, khoảng cách tại y `158`, tên đường tại y `176`, vị trí hiện tại tại y `190`, dòng ETA/TPMS tại `BOTTOM_MID -22`.
- Dòng mới `nav_state_label` tại `BOTTOM_MID -4`, hiển thị `navState: <giá trị>`.

API mới `ui_set_nav_state(int)` trong `ui_screens.h/.c`; `nav_state < 0` sẽ xóa dòng này. Để đưa dữ liệu tới đó, `nav_data_t` thêm field `int16_t nav_state`; parser `VMSX` gán giá trị thật, còn đường JSON `nav` cũ gán `-1` để không hiển thị sai. `app_main.on_nav_data()` gọi `ui_set_nav_state()` khi `nav_state >= 0`.

Firmware build lại thành công (`esp32mvn.bin`, `0x110320`, còn 65% partition) và đã flash. Sau khi board reset và app kết nối lại, serial xác nhận đường hiển thị hoạt động:

```text
VMSL hop le: speed_limit=0  current_speed=0
VMSL hop le: speed_limit=50 current_speed=0
app_main: Gioi han toc do doi: 0 -> 50 km/h
```

Dòng `app_main` chứng minh `on_car_data()` đã chạy và `ui_car_update()` đã vẽ lại hai vòng số mới. Dòng `navState` chỉ xuất hiện khi có frame `VMSX`, tức khi Android Auto đang kết nối; ở lần capture này Android Auto đã ngắt nên chưa kiểm chứng được giá trị hiển thị.


Capture sau khi kết nối lại Android Auto với firmware layout mới:

```text
VMSX hop le: limit=50 speed=1 min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=0 min=0 nav_state=0 over=0 under_min=0 hud=0
VMSX hop le: limit=50 speed=2 min=0 nav_state=0 over=0 under_min=0 hud=0
```

Vì `nav_state = 0` và điều kiện phát là `nav_state >= 0`, `on_nav_data()` được gọi mỗi frame và `ui_set_nav_state(0)` cập nhật dòng dưới cùng thành `navState: 0`. Giá trị vẫn là 0 do chưa chạy lộ trình dẫn đường; cần một chuyến chỉ đường thật để thấy `navigationState` đổi giá trị.
