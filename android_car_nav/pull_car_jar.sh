#!/bin/bash
#
# pull_car_jar.sh - Pull android.car.jar từ thiết bị Android để compile app
#
# File android.car.jar chứa Car API classes. Cần cho compile-time reference.
# Trên thiết bị Android Automotive, file này nằm ở:
#   /system/framework/android.car.jar
#
# Sử dụng:
#   ./pull_car_jar.sh
#
# Output: app/libs/android.car.jar
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/app/libs"
OUTPUT_FILE="${OUTPUT_DIR}/android.car.jar"

mkdir -p "$OUTPUT_DIR"

echo "📱 Pull android.car.jar từ thiết bị..."

# Thử các vị trí phổ biến
LOCATIONS=(
    "/system/framework/android.car.jar"
    "/system/framework/car-lib.jar"
    "/system_ext/framework/android.car.jar"
    "/product/framework/android.car.jar"
)

PULLED=false
for LOC in "${LOCATIONS[@]}"; do
    echo "   Thử: $LOC"
    if adb shell "test -f $LOC" 2>/dev/null; then
        adb pull "$LOC" "$OUTPUT_FILE"
        PULLED=true
        echo "✅ Pulled từ $LOC"
        break
    fi
done

if [ "$PULLED" = false ]; then
    echo ""
    echo "⚠️  Không tìm thấy android.car.jar trên thiết bị."
    echo ""
    echo "Có thể thiết bị không phải Android Automotive."
    echo "Bạn có thể:"
    echo "  1. Download từ AOSP source:"
    echo "     https://android.googlesource.com/platform/packages/services/Car/"
    echo ""
    echo "  2. Tạo stub jar (chỉ cần compile, runtime dùng system jar):"
    echo "     Xem README trong app/libs/"
    echo ""
    echo "  3. Nếu thiết bị có Car Service nhưng jar ở vị trí khác:"
    echo "     adb shell find /system -name '*.car*' -o -name '*car-lib*'"
    echo ""

    # Tạo placeholder README
    cat > "${OUTPUT_DIR}/README.md" << 'EOF'
# android.car.jar

File này cần cho compile-time reference khi build app.

## Cách lấy:

### 1. Pull từ thiết bị Android Automotive:
```bash
adb pull /system/framework/android.car.jar app/libs/android.car.jar
```

### 2. Build từ AOSP source:
```bash
# Clone car-lib
git clone https://android.googlesource.com/platform/packages/services/Car/
# Build android.car.jar
```

### 3. Download pre-built (API 33):
Tìm trên https://mvnrepository.com hoặc GitHub repos có sẵn car-lib stub.

## Lưu ý
- File này chỉ dùng lúc compile (compileOnly dependency)
- Runtime sẽ dùng framework jar có sẵn trên thiết bị
- Nếu không có file này, các class trong package `android.car.*` sẽ không resolve được
EOF
    echo "📝 Đã tạo README tại ${OUTPUT_DIR}/README.md"
    exit 1
fi

echo ""
echo "📁 Output: $OUTPUT_FILE"
echo "   Size: $(du -h "$OUTPUT_FILE" | cut -f1)"
echo ""
echo "✅ Sẵn sàng build app. Chạy: ./gradlew assembleRelease"
