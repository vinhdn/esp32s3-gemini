#!/usr/bin/env bash
# Cài đặt bản APK mới nhất của VietMap Live (đã patch + resign) và CarNav (ESP32 HUD
# companion app, bản RELEASE - ký bằng app/car-nav.jks) lên Android head unit qua
# adb, rồi cấp full quyền cho cả 2 app.
#
# CarNav trỏ tới app-release.apk (không phải app-debug.apk) - build trước khi
# chạy script này bằng:
#   cd esp32/android_car_nav && ./gradlew.bat :app:assembleRelease
#
# Nếu app đã cài trên máy với signature KHÁC (ví dụ bản gốc chưa patch, hoặc bản
# patch ký bằng key khác lần trước, hoặc trước đó cài bản debug rồi giờ chuyển
# sang bản release - debug/release luôn ký bằng key khác nhau) thì
# `adb install -r` sẽ báo lỗi INSTALL_FAILED_UPDATE_INCOMPATIBLE / "signatures do
# not match" — script tự phát hiện lỗi này, gỡ app cũ rồi cài lại từ đầu (MẤT
# DATA của app đó, không tránh được vì đây là giới hạn của Android khi đổi
# signing key).
#
# Dùng: ./install_headunit.sh [device-serial]
#   Không truyền serial nếu chỉ có 1 thiết bị/head unit đang kết nối qua adb.
#   Nếu có nhiều thiết bị, truyền serial lấy từ `adb devices`.

set -u

SERIAL="${1:-}"
if [ -n "$SERIAL" ]; then
    ADB=(adb -s "$SERIAL")
else
    ADB=(adb)
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VIETMAP_APK="$SCRIPT_DIR/analysis/vietmap-live-3.3.4-bubble2-signed.apk"
VIETMAP_PKG="vn.vietmap.live"

CARNAV_APK="$SCRIPT_DIR/esp32/android_car_nav/app/build/outputs/apk/release/app-release.apk"
CARNAV_PKG="com.esp32nav"

# Quyền "dangerous" (runtime) — lấy từ `aapt dump badging` của từng APK. Quyền
# normal/signature (INTERNET, FOREGROUND_SERVICE, android.car.permission.*...)
# không grant được qua pm grant nên không đưa vào đây; -g lúc install đã cố
# gắng cấp hết những gì cấp được, loop pm grant dưới đây chỉ để chắc chắn.
VIETMAP_PERMS=(
    android.permission.ACCESS_COARSE_LOCATION
    android.permission.ACCESS_FINE_LOCATION
    android.permission.ACCESS_BACKGROUND_LOCATION
    android.permission.RECORD_AUDIO
    android.permission.READ_EXTERNAL_STORAGE
    android.permission.WRITE_EXTERNAL_STORAGE
    android.permission.READ_PHONE_STATE
    android.permission.READ_MEDIA_AUDIO
    android.permission.CAMERA
    android.permission.POST_NOTIFICATIONS
    android.permission.BLUETOOTH_SCAN
    android.permission.BLUETOOTH_CONNECT
    android.permission.BLUETOOTH_ADVERTISE
)

CARNAV_PERMS=(
    android.permission.ACCESS_FINE_LOCATION
    android.permission.ACCESS_COARSE_LOCATION
    android.permission.POST_NOTIFICATIONS
    android.permission.BLUETOOTH_SCAN
    android.permission.BLUETOOTH_CONNECT
    android.permission.BLUETOOTH_ADVERTISE
)

log() { echo ">> $*"; }

# install_or_replace <apk> <package>
# Cài -r -g (reinstall, tự grant runtime permission). Nếu lỗi do signature
# khác bản đã cài, gỡ app rồi cài lại từ đầu.
install_or_replace() {
    local apk="$1" pkg="$2"
    if [ ! -f "$apk" ]; then
        echo "!! Không tìm thấy APK: $apk" >&2
        return 1
    fi

    log "Cài $pkg từ $(basename "$apk") ..."
    local out
    out="$("${ADB[@]}" install -r -g "$apk" 2>&1)"
    echo "$out"

    if echo "$out" | grep -qi "INSTALL_FAILED_UPDATE_INCOMPATIBLE\|signatures do not match\|INSTALL_FAILED_SHARED_USER_INCOMPATIBLE"; then
        log "Signature khác bản đã cài -> gỡ $pkg rồi cài lại (mất data cũ của app này)"
        "${ADB[@]}" uninstall "$pkg"
        out="$("${ADB[@]}" install -g "$apk" 2>&1)"
        echo "$out"
    fi

    if ! echo "$out" | grep -qi "^Success"; then
        echo "!! Cài $pkg thất bại, xem log ở trên." >&2
        return 1
    fi
    return 0
}

# grant_all <package> <perm...>
grant_all() {
    local pkg="$1"; shift
    for perm in "$@"; do
        "${ADB[@]}" shell pm grant "$pkg" "$perm" >/dev/null 2>&1 \
            && log "  grant $perm OK" \
            || log "  grant $perm bỏ qua (không cần/không hỗ trợ trên thiết bị này)"
    done
}

log "=== Kiểm tra thiết bị ==="
"${ADB[@]}" devices -l
"${ADB[@]}" wait-for-device

log "=== 1. VietMap Live (đã patch + resign) ==="
if install_or_replace "$VIETMAP_APK" "$VIETMAP_PKG"; then
    grant_all "$VIETMAP_PKG" "${VIETMAP_PERMS[@]}"
    # SYSTEM_ALERT_WINDOW là app-op đặc biệt (bubble overlay), không cấp được
    # qua pm grant — phải dùng appops.
    "${ADB[@]}" shell appops set "$VIETMAP_PKG" SYSTEM_ALERT_WINDOW allow \
        && log "  appops SYSTEM_ALERT_WINDOW allow OK"
    "${ADB[@]}" shell appops set "$VIETMAP_PKG" REQUEST_INSTALL_PACKAGES allow >/dev/null 2>&1
fi

log "=== 2. CarNav (ESP32 HUD companion) ==="
if install_or_replace "$CARNAV_APK" "$CARNAV_PKG"; then
    grant_all "$CARNAV_PKG" "${CARNAV_PERMS[@]}"

    # Accessibility Service + Notification Listener KHÔNG phải permission
    # thường, bị Android tự tắt mỗi lần install/force-stop — CarNav cần cả 2
    # để đọc bong bóng VietMap Live (bubble overlay + cảnh báo camera).
    log "  bật Accessibility Service (đọc bong bóng VietMap)"
    "${ADB[@]}" shell settings put secure enabled_accessibility_services \
        "$CARNAV_PKG/com.esp32nav.service.VietmapAccessibilityService"
    "${ADB[@]}" shell settings put secure accessibility_enabled 1

    log "  bật Notification Listener (đọc cảnh báo camera)"
    "${ADB[@]}" shell cmd notification allow_listener \
        "$CARNAV_PKG/com.esp32nav.service.NavigationListenerService"

    # Mở app 1 lần: quan trọng trên app MỚI CÀI (hoặc vừa force-stop) vì
    # Android giữ app ở trạng thái "stopped" cho tới khi có 1 lần mở tay/qua
    # am start — trong lúc "stopped" thì broadcast BOOT_COMPLETED (BootReceiver)
    # KHÔNG được gửi tới app, nên lần reboot đầu tiên sau khi cài sẽ không tự
    # khởi động được app. Mở 1 lần ở đây để lần reboot sau đó luôn tự chạy.
    log "  mở app CarNav (MainActivity)"
    "${ADB[@]}" shell am start -n "$CARNAV_PKG/.MainActivity"

    # BleForegroundService là exported="false" nên KHÔNG gọi được trực tiếp
    # qua `am start-foreground-service` từ adb shell (lỗi "not exported").
    # Không cần thiết: MainActivity.onCreate() -> checkAndRequestPermissions()
    # đã tự gọi startForegroundService() ngay khi đủ quyền (đã grant ở trên),
    # và CarNavApplication.onCreate() cũng tự gọi lại sau 2s dù app được mở
    # theo cách nào — xác nhận qua dumpsys activity services (isForeground=true)
    # ngay sau khi lệnh am start ở trên chạy xong.
    sleep 2
    log "  kiểm tra BleForegroundService đã chạy foreground chưa"
    "${ADB[@]}" shell dumpsys activity services "$CARNAV_PKG" \
        | grep -q "BleForegroundService" \
        && log "  OK - service đang chạy" \
        || log "  !! service CHƯA thấy chạy, kiểm tra lại thủ công"
fi

log "=== Xong ==="