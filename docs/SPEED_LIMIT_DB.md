# Speed-limit GPS database

Firmware embeds `main/speed_limit/speed_limit_data.bin`, generated from the decoded Edogen CSV:

```bash
python3 tools/generate_speed_limit_db.py /tmp/edogen-decoded.csv \
  main/speed_limit/speed_limit_data.bin
idf.py build
```

Only rows with `type_code=1` and `value_or_speed>0` are retained. Coordinates are E6 signed integers and each point occupies 12 bytes (`lon`, `lat`, `bearing`, `speed`). The current dataset contains 16,400 points (196,812 bytes including its header).

Android sends one HLP/1 message per GPS fix:

```json
{"v":1,"t":"gps","lon":104.847283,"lat":8.576372,"bearing":304.0,"bearing_valid":true,"spd":42,"acc":5.0}
```

The ESP32 uses the point bearing to reject the opposite carriageway. Every GPS fix is recalculated independently: it prefers the nearest direction-compatible point already passed and falls back to the nearest matching point. This prevents an old limit from remaining latched when sparse data or a curved road produces a large lateral projection. The next limit is the nearest different-speed point ahead within 5 km. Its displayed distance is straight-line/local projected distance, so it is an estimate rather than routed road distance.

## Kiểm thử vị trí trên Android

Nhấn biểu tượng vị trí trên thanh tiêu đề của ứng dụng Android để mở bản đồ OpenStreetMap. Khi màn hình này mở, GPS thật tạm ngừng gửi tới board để không ghi đè điểm kiểm thử. Chạm bản đồ, đặt hướng di chuyển và tốc độ, rồi nhấn **Gửi tới board**. Board phản hồi lại:

- giới hạn hiện tại, giới hạn tiếp theo và khoảng cách;
- khoảng cách từ GPS tới record được chọn;
- độ lệch ngang và độ lệch bearing;
- confidence `high`, `medium`, `low`, hoặc `none`.

Lookup vẫn tìm candidate trong 5 km để có thể báo diagnostics, nhưng chỉ hiển thị current limit khi match đạt tối thiểu `medium`: GPS có bearing phải cách tối đa 500 m, lệch ngang tối đa 150 m và lệch hướng tối đa 40°. Không có bearing thì khoảng cách tối đa là 150 m. Candidate next-limit bị loại nếu lệch ngang quá 300 m hoặc lệch hướng quá 45°. Match trong 150 m, lệch ngang tối đa 75 m và lệch hướng tối đa 20° được phân loại `high`.

## Kết quả audit và giới hạn dữ liệu

Chạy audit tái lập được bằng:

```bash
python3 tools/evaluate_speed_limit_db.py /tmp/edogen-decoded.csv
```

Dataset hiện có 16.400 speed records và không có tọa độ+bearing trùng nhau nhưng khác speed. Kiểm tra cặp gần cho thấy chỉ 6 cặp khác speed nằm trong 20 m và có bearing gần nhau; đây không phải nguyên nhân chính của sai số quan sát được.

Nguyên nhân chính là dữ liệu point-only thưa và không có road/segment ID. Trong chuỗi GPS thực tế đã kiểm tra, thuật toán cũ nhận các record cách 894–3.205 m với lệch ngang 417–2.040 m, nên có thể lấy biển của đường song song. Các candidate đó nay trả về `confidence=none` thay vì một tốc độ có vẻ chính xác nhưng sai. Không thể suy ra chắc chắn road hiện tại hoặc routed distance chỉ từ sáu cột CSV; muốn nâng độ chính xác tiếp theo cần dữ liệu road geometry/segment ID hoặc map matching trên graph đường.

## Dual lookup Android / ESP32

APK nhúng byte-for-byte cùng `speed_limit_data.bin` trong `app/src/main/assets` và chạy thuật toán Kotlin tương đương firmware trước mỗi lần gửi GPS. GPS HLP mang thêm kết quả Android (`app_match`, `app_candidate`, current/next và diagnostics). ESP32 vẫn lookup độc lập trên mọi fix, ghi log `CANDIDATE MATCH`/`MISMATCH`, rồi phản hồi kết quả board qua `t=speed_limit` để màn hình map hiển thị hai phía cạnh nhau.

Chính sách hiển thị:

- GPS thật: dùng kết quả Android khi đạt confidence; nếu Android không match nhưng board match thì fallback board.
- Map tester: công tắc **Hiển thị candidate Android trên board** cho phép hiển thị raw candidate dù không đạt confidence. Response ghi `display_src=android_test`. Chế độ này chỉ dành cho kiểm tra dữ liệu vì candidate có thể nằm trên đường khác.
- Tắt công tắc để kiểm tra chính sách an toàn giống GPS thật.

Dual GPS frame dài hơn một ATT payload (MTU 247, payload 244), nên firmware duy trì trạng thái JSON continuation đến newline để ghép nhiều GATT writes. Không được phân loại continuation chunk theo byte đầu tiên vì chunk thứ hai không bắt đầu bằng `{`.
