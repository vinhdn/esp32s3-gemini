# VIETMAP Live → ESP32 HUD

Repository này lưu phần tái sử dụng để port relay tốc độ từ các bản VIETMAP Live mới sang ESP32 HUD. APK gốc/đã ký, cây apktool, firmware build và signing key đều là artifact local, không được commit.

## Thành phần

- `analysis/REPORT_VI.md`: kết quả reverse engineering, frame `VMSL`/`VMSX`, anchor và kiểm chứng runtime.
- `analysis/capture_vietmap_h50.{py,js}`: capture GATT/plaintext bằng Frida.
- `analysis/capture_esp32_serial.py`: capture serial ESP32 không cần pyserial.
- `patches/vietmap-live-3.3.4/`: patch host Smali và quy trình port sang APK mới.
- `analysis/apktool-3.3.4/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali`: helper Smali duy nhất được giữ từ cây decompile, dùng làm template.
- `esp32/`: submodule firmware, ghim vào commit có parser/UI `VMSL`/`VMSX`.

Clone kèm firmware:

```sh
git clone --recurse-submodules <repository-url>
```

Nếu đã clone:

```sh
git submodule update --init --recursive
```

Xem `patches/vietmap-live-3.3.4/README.md` để port/build APK. Signing key và credential phải được cung cấp local; không lưu trong Git hoặc command mẫu có thể bị ghi vào shell history.
