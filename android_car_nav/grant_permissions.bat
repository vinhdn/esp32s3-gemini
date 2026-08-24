@echo off
REM ============================================================
REM  Grant all permissions for com.esp32nav via ADB
REM  Usage: grant_permissions.bat
REM  Note: Device must be connected and ADB authorized
REM ============================================================

set PACKAGE=com.esp32nav

echo ============================================================
echo  Granting permissions for %PACKAGE%
echo ============================================================
echo.

REM --- Bluetooth permissions ---
echo [BLE] Granting BLUETOOTH_SCAN...
adb shell pm grant %PACKAGE% android.permission.BLUETOOTH_SCAN
echo [BLE] Granting BLUETOOTH_CONNECT...
adb shell pm grant %PACKAGE% android.permission.BLUETOOTH_CONNECT
echo [BLE] Granting ACCESS_FINE_LOCATION...
adb shell pm grant %PACKAGE% android.permission.ACCESS_FINE_LOCATION

REM --- Notification permission (Android 13+) ---
echo [NOTIFICATION] Granting POST_NOTIFICATIONS...
adb shell pm grant %PACKAGE% android.permission.POST_NOTIFICATIONS

echo.
echo ============================================================
echo  Enabling Notification Listener Service
echo ============================================================
echo.

REM --- Enable NotificationListenerService ---
echo [SERVICE] Enabling NotificationListenerService...
adb shell cmd notification allow_listener %PACKAGE%/com.esp32nav.service.NavigationListenerService

echo.
echo ============================================================
echo  Enabling Accessibility Service
echo ============================================================
echo.

REM --- Enable AccessibilityService ---
echo [SERVICE] Enabling VietmapAccessibilityService...
adb shell settings put secure enabled_accessibility_services %PACKAGE%/com.esp32nav.service.VietmapAccessibilityService
adb shell settings put secure accessibility_enabled 1

echo.
echo ============================================================
echo  Granting CAR permissions (requires system/priv-app)
echo ============================================================
echo.

REM --- Car permissions (only works if app is installed as priv-app) ---
echo [CAR] Granting CAR_SPEED...
adb shell pm grant %PACKAGE% android.car.permission.CAR_SPEED
echo [CAR] Granting CAR_ENGINE_DETAILED...
adb shell pm grant %PACKAGE% android.car.permission.CAR_ENGINE_DETAILED
echo [CAR] Granting CAR_TIRES...
adb shell pm grant %PACKAGE% android.car.permission.CAR_TIRES
echo [CAR] Granting CAR_ENERGY...
adb shell pm grant %PACKAGE% android.car.permission.CAR_ENERGY
echo [CAR] Granting CAR_DYNAMICS_STATE...
adb shell pm grant %PACKAGE% android.car.permission.CAR_DYNAMICS_STATE
echo [CAR] Granting CAR_INFO...
adb shell pm grant %PACKAGE% android.car.permission.CAR_INFO
echo [CAR] Granting CAR_EXTERIOR_ENVIRONMENT...
adb shell pm grant %PACKAGE% android.car.permission.CAR_EXTERIOR_ENVIRONMENT
echo [CAR] Granting CAR_VENDOR_EXTENSION...
adb shell pm grant %PACKAGE% android.car.permission.CAR_VENDOR_EXTENSION
echo [CAR] Granting READ_CAR_VENDOR_PERMISSION_INFO...
adb shell pm grant %PACKAGE% android.car.permission.READ_CAR_VENDOR_PERMISSION_INFO

echo.
echo ============================================================
echo  Done! Verifying granted permissions...
echo ============================================================
echo.

adb shell dumpsys package %PACKAGE% | findstr "permission"

echo.
echo [TIP] If CAR permissions failed, the app needs to be
echo       installed as a privileged system app:
echo       adb root
echo       adb remount
echo       adb push app-release.apk /system/priv-app/CarNavBle/CarNavBle.apk
echo       adb reboot
echo.
pause
