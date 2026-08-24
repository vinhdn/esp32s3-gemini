#!/bin/bash
#
# install_priv_app.sh - Đẩy APK vào /system/priv-app trên thiết bị Android (root)
#
# Sử dụng:
#   ./install_priv_app.sh [path/to/apk]
#
# Nếu không truyền path APK, script sẽ tìm trong app/build/outputs/apk/release/
# hoặc app/build/outputs/apk/debug/
#
# Yêu cầu:
#   - adb đã kết nối tới thiết bị
#   - Thiết bị đã root (su hoạt động)
#   - USB debugging đã bật
#
# Script sẽ:
#   1. Remount /system read-write
#   2. Tạo /system/priv-app/CarNavBLE/
#   3. Push APK vào đó
#   4. Push privapp-permissions XML vào /system/etc/permissions/
#   5. Set đúng permissions (644) và SELinux context
#   6. Reboot thiết bị để apply
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_PACKAGE="com.esp32nav"
APP_DIR_NAME="CarNavBLE"
PRIV_APP_PATH="/system/priv-app/${APP_DIR_NAME}"
PERMISSIONS_XML="privapp-permissions-esp32nav.xml"
PERMISSIONS_PATH="/system/etc/permissions/${PERMISSIONS_XML}"

# Tìm APK
APK_PATH="$1"
if [ -z "$APK_PATH" ]; then
    # Thử release trước
    APK_PATH="${SCRIPT_DIR}/app/build/outputs/apk/release/app-release.apk"
    if [ ! -f "$APK_PATH" ]; then
        # Fallback debug
        APK_PATH="${SCRIPT_DIR}/app/build/outputs/apk/debug/app-debug.apk"
    fi
fi

if [ ! -f "$APK_PATH" ]; then
    echo "❌ Không tìm thấy APK."
    echo "   Build trước: ./gradlew assembleRelease (hoặc assembleDebug)"
    echo "   Hoặc truyền đường dẫn: $0 path/to/app.apk"
    exit 1
fi

echo "📱 APK: $APK_PATH"
echo "📁 Target: $PRIV_APP_PATH"
echo ""

# Kiểm tra adb
if ! command -v adb &> /dev/null; then
    echo "❌ adb không tìm thấy. Cài Android SDK Platform-Tools."
    exit 1
fi

# Kiểm tra thiết bị kết nối
DEVICE_COUNT=$(adb devices | grep -c "device$" || true)
if [ "$DEVICE_COUNT" -eq 0 ]; then
    echo "❌ Không có thiết bị Android nào kết nối."
    echo "   Bật USB debugging và kết nối cáp USB."
    exit 1
fi

echo "✓ Thiết bị đã kết nối"
adb devices | grep "device$"
echo ""

# Gỡ app cũ nếu đã cài bình thường (tránh conflict)
echo "🗑️  Gỡ cài đặt cũ (nếu có)..."
adb shell pm uninstall "$APP_PACKAGE" 2>/dev/null || true

# Remount /system read-write
echo "🔓 Remount /system rw..."
adb shell "su -c 'mount -o remount,rw /system'" 2>/dev/null || \
adb shell "su -c 'mount -o remount,rw /'" 2>/dev/null || \
adb remount 2>/dev/null || {
    echo "⚠️  Thử disable-verity + reboot nếu remount thất bại..."
    adb disable-verity 2>/dev/null || true
    echo "   Cần reboot rồi chạy lại script."
    exit 1
}

# Tạo thư mục priv-app
echo "📂 Tạo $PRIV_APP_PATH..."
adb shell "su -c 'mkdir -p ${PRIV_APP_PATH}'"

# Push APK
echo "📦 Push APK..."
adb push "$APK_PATH" /data/local/tmp/CarNavBLE.apk
adb shell "su -c 'cp /data/local/tmp/CarNavBLE.apk ${PRIV_APP_PATH}/CarNavBLE.apk'"
adb shell "su -c 'chmod 644 ${PRIV_APP_PATH}/CarNavBLE.apk'"
adb shell "su -c 'chown root:root ${PRIV_APP_PATH}/CarNavBLE.apk'"

# Push permissions XML
echo "📋 Push permissions XML..."
PERM_XML_LOCAL="${SCRIPT_DIR}/${PERMISSIONS_XML}"
if [ ! -f "$PERM_XML_LOCAL" ]; then
    echo "❌ Không tìm thấy $PERM_XML_LOCAL"
    exit 1
fi
adb push "$PERM_XML_LOCAL" /data/local/tmp/${PERMISSIONS_XML}
adb shell "su -c 'cp /data/local/tmp/${PERMISSIONS_XML} ${PERMISSIONS_PATH}'"
adb shell "su -c 'chmod 644 ${PERMISSIONS_PATH}'"
adb shell "su -c 'chown root:root ${PERMISSIONS_PATH}'"

# Set SELinux context (nếu enforce)
echo "🔒 Set SELinux context..."
adb shell "su -c 'chcon u:object_r:system_file:s0 ${PRIV_APP_PATH}/CarNavBLE.apk'" 2>/dev/null || true
adb shell "su -c 'chcon u:object_r:system_file:s0 ${PERMISSIONS_PATH}'" 2>/dev/null || true

# Cleanup temp
adb shell "rm -f /data/local/tmp/CarNavBLE.apk /data/local/tmp/${PERMISSIONS_XML}"

echo ""
echo "✅ Hoàn tất! APK đã được đặt vào priv-app."
echo ""
echo "📍 Files trên thiết bị:"
echo "   APK:         $PRIV_APP_PATH/CarNavBLE.apk"
echo "   Permissions: $PERMISSIONS_PATH"
echo ""

# Hỏi reboot
read -p "🔄 Reboot thiết bị ngay? (y/N): " REBOOT
if [[ "$REBOOT" =~ ^[Yy]$ ]]; then
    echo "Rebooting..."
    adb reboot
    echo "⏳ Đợi thiết bị khởi động lại..."
else
    echo ""
    echo "⚠️  Cần reboot để hệ thống nhận app mới:"
    echo "   adb reboot"
    echo ""
    echo "Sau khi reboot, kiểm tra permissions:"
    echo "   adb shell dumpsys package com.esp32nav | grep permission"
fi
