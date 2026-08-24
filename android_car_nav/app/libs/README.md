# android.car.jar

File này cần cho compile-time reference khi build app với Car API.

## Cách lấy:

### 1. Pull từ thiết bị Android Automotive (khuyến nghị):
```bash
# Chạy script tự động:
./pull_car_jar.sh

# Hoặc thủ công:
adb pull /system/framework/android.car.jar app/libs/android.car.jar
```

### 2. Từ Android Emulator (Automotive):
Dùng Android Studio tạo emulator Automotive (Polestar 2, Generic Car),
rồi pull từ emulator.

### 3. Từ AOSP source:
```bash
# Build car-lib từ source
# Output: out/target/common/obj/JAVA_LIBRARIES/android.car_intermediates/classes.jar
```

## Lưu ý
- Đây là `compileOnly` dependency - chỉ dùng lúc compile, KHÔNG đóng gói vào APK
- Runtime: hệ thống Android Automotive cung cấp framework jar `/system/framework/android.car.jar`
- Nếu chạy trên thiết bị KHÔNG có Car Service, VhalManager sẽ catch exception và báo UNAVAILABLE
- App vẫn hoạt động bình thường (fallback sang OBD-II qua Bluetooth)
