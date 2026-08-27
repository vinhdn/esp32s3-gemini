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
  - `Ma/j.u()`: giữ kết nối khi relay còn state/GATT hợp lệ.
  - `pa/f.q(JSONObject)`: lấy `VMSX` từ Android Auto theo JSON key.
  - `MainActivity`: cập nhật `VMSL` phone-only khi speed/limit thay đổi.
- Helper template: `analysis/apktool-3.3.4/smali_classes2/vn/vietmap/live/patch/VmslRelay.smali`.

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
```

Nếu dry-run fail, port thủ công đúng năm hook nêu trên rồi kiểm tra register count/label trong từng method.

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
