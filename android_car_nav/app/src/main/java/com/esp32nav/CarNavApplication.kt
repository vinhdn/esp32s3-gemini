package com.esp32nav

import android.app.Application
import android.content.Intent
import com.esp32nav.ble.BleManager
import com.esp32nav.carhost.ImageRelayBle
import com.esp32nav.model.NavigationData
import com.esp32nav.model.VehicleData
import com.esp32nav.obd.ObdManager
import com.esp32nav.parser.DatMapParser
import com.esp32nav.stream.MapStreamManager
import com.esp32nav.vhal.VhalManager
import com.esp32nav.weather.WeatherManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.text.Normalizer

class CarNavApplication : Application() {

    lateinit var bleManager: BleManager
        private set

    lateinit var obdManager: ObdManager
        private set

    lateinit var vhalManager: VhalManager
        private set

    lateinit var mapStreamManager: MapStreamManager
        private set

    // Kết nối BLE riêng cho luồng gửi bitmap bong bóng — KHÔNG dùng chung
    // BleManager (giao thức HLP cũ, không khớp firmware hiện tại). Xem
    // esp32/android_car_nav/carhost/ImageRelayBle.kt.
    lateinit var imageRelay: ImageRelayBle
        private set

    // Thời tiết hiển thị thay vòng tròn biển báo khi không có dữ liệu tốc
    // độ giới hạn (xem VietmapAccessibilityService.kt).
    lateinit var weatherManager: WeatherManager
        private set

    private val _currentNavData = MutableStateFlow<NavigationData?>(null)
    val currentNavData: StateFlow<NavigationData?> = _currentNavData.asStateFlow()

    private val _currentDatMapData = MutableStateFlow<DatMapParser.DatMapData?>(null)
    val currentDatMapData: StateFlow<DatMapParser.DatMapData?> = _currentDatMapData.asStateFlow()

    private val _messageLog = MutableStateFlow<List<LogEntry>>(emptyList())
    val messageLog: StateFlow<List<LogEntry>> = _messageLog.asStateFlow()

    // Source ưu tiên: VHAL > OBD. Nếu VHAL connected thì dùng VHAL, không thì OBD.
    private val _activeVehicleSource = MutableStateFlow("none") // "vhal", "obd", "none"
    val activeVehicleSource: StateFlow<String> = _activeVehicleSource.asStateFlow()

    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
    private var lastVehicleDataSent: VehicleData? = null

    override fun onCreate() {
        super.onCreate()
        bleManager = BleManager(this)
        obdManager = ObdManager(this)
        vhalManager = VhalManager(this)
        imageRelay = ImageRelayBle(this)
        mapStreamManager = MapStreamManager(this, imageRelay)
        weatherManager = WeatherManager(this)

        // Kết nối tới board (BleManager.startScan() + ImageRelayBle.start())
        // CHỈ được khởi động bên trong BleForegroundService (onCreate/onDestroy)
        // — không ở đây, không ở Activity. Application.onCreate() chạy bất cứ
        // khi nào process được tạo (kể cả do AccessibilityService/BootReceiver
        // khởi tạo process độc lập với việc user mở app), nên gọi startScan()
        // ở đây từng gây đăng ký scan trùng lặp/cạn tài nguyên BLE của hệ
        // thống. Ở đây chỉ đảm bảo service (nơi thực sự quản lý kết nối)
        // được khởi động.
        scope.launch {
            // Delay nhỏ cho hệ thống ổn định sau boot
            kotlinx.coroutines.delay(2000)
            startForegroundService()
        }

        // Thử kết nối VHAL (sẽ thành công trên Android Automotive)
        vhalManager.connect()

        // Start LogcatReader để đọc TPMS + speed limit từ InstrumentActivity
        startLogcatReader()

        // Observe VHAL vehicle data
        scope.launch {
            vhalManager.vehicleData.collect { data ->
                if (vhalManager.connectionState.value == VhalManager.VhalConnectionState.CONNECTED) {
                    _activeVehicleSource.value = "vhal"
                    sendVehicleDataIfChanged(data)
                }
            }
        }

        // Observe OBD vehicle data (fallback khi VHAL không khả dụng)
        scope.launch {
            obdManager.vehicleData.collect { data ->
                if (vhalManager.connectionState.value != VhalManager.VhalConnectionState.CONNECTED) {
                    if (obdManager.connectionState.value == ObdManager.ObdConnectionState.CONNECTED) {
                        _activeVehicleSource.value = "obd"
                        sendVehicleDataIfChanged(data)
                    }
                }
            }
        }

        // Khi VHAL disconnect, fallback về OBD
        scope.launch {
            vhalManager.connectionState.collect { state ->
                if (state == VhalManager.VhalConnectionState.UNAVAILABLE ||
                    state == VhalManager.VhalConnectionState.DISCONNECTED) {
                    if (obdManager.connectionState.value != ObdManager.ObdConnectionState.CONNECTED) {
                        _activeVehicleSource.value = "none"
                    }
                }
            }
        }
    }

    private fun sendVehicleDataIfChanged(data: VehicleData) {
        if (!data.hasData()) return
        if (data == lastVehicleDataSent) return
        lastVehicleDataSent = data

        val message = data.toHlpJson()
        bleManager.writeData(message)
        addLogEntry("TX", message.trim())
    }

    fun onNavigationUpdate(data: NavigationData) {
        val previous = _currentNavData.value
        if (previous != data) {
            _currentNavData.value = data
            bleManager.sendNavigation(data)
            addLogEntry("TX", data.toHlpJson().trim())
        }
    }

    /**
     * Nhận dữ liệu từ DatMap (tốc độ giới hạn, cảnh báo camera).
     * Gửi sang ESP32 dưới dạng message type "lim" (speed limit alert).
     */
    fun onDatMapUpdate(data: DatMapParser.DatMapData) {
        val previous = _currentDatMapData.value
        if (previous?.speedLimit == data.speedLimit &&
            previous?.cameraDistance == data.cameraDistance) return

        _currentDatMapData.value = data

        // Gửi message type "lim" sang ESP32
        val json = buildString {
            append("{\"v\":1,\"t\":\"lim\"")
            if (data.speedLimit > 0) append(",\"limit\":${data.speedLimit}")
            if (data.cameraDistance > 0) append(",\"cam_dist\":${data.cameraDistance}")
            if (data.cameraType.isNotBlank()) append(",\"cam_type\":\"${data.cameraType}\"")
            if (data.alertMessage.isNotBlank()) {
                val escaped = data.alertMessage.replace("\"", "\\\"").take(100)
                append(",\"msg\":\"$escaped\"")
            }
            append("}\n")
        }
        bleManager.writeData(json)
        addLogEntry("TX", json.trim())
    }

    /**
     * Normalize Unicode text sang NFC (precomposed form).
     * Cần thiết vì Android/Google Maps có thể trả về NFD (decomposed: e + combining ^)
     * trong khi font ESP32 chỉ có NFC (precomposed: ế = U+1EBF).
     */
    private fun nfc(text: String): String = Normalizer.normalize(text, Normalizer.Form.NFC)

    /**
     * Nhận dữ liệu từ AccessibilityService (Vietmap Live / DatMap / Google Maps).
     * Gửi sang ESP32: tốc độ giới hạn, tốc độ hiện tại, tên đường.
     * Message type "s" cho speed data (tương thích với car_update trên ESP32).
     */
    fun onAccessibilityUpdate(speedLimit: Int, currentSpeed: Int, roadName: String, source: String) {
        val json = buildString {
            append("{\"v\":1,\"t\":\"s\"")
            if (currentSpeed >= 0) append(",\"spd\":$currentSpeed")
            if (speedLimit > 0) append(",\"lim\":$speedLimit")
            if (roadName.isNotBlank()) {
                val escaped = nfc(roadName).replace("\"", "\\\"").replace("\n", " ").take(60)
                append(",\"road\":\"$escaped\"")
            }
            append(",\"src\":\"$source\"")
            append("}\n")
        }
        bleManager.writeData(json)
        addLogEntry("TX", json.trim())
    }

    fun onNavigationCleared() {
        _currentNavData.value = null
    }

    /**
     * Start LogcatReader service để đọc TPMS + speed limit từ InstrumentActivity log.
     */
    private fun startLogcatReader() {
        val intent = Intent(this, com.esp32nav.service.LogcatReaderService::class.java)
        try {
            startService(intent)
        } catch (e: IllegalStateException) {
            android.util.Log.e("CarNavApp", "Cannot start LogcatReader: ${e.message}")
        }
    }

    /**
     * Nhận TPMS data từ LogcatReader (đọc logcat InstrumentActivity).
     * Gửi qua BLE sang ESP32 dưới dạng vehicle data.
     */
    fun onLogcatTpmsUpdate(fl: Int, fr: Int, rl: Int, rr: Int) {
        val data = VehicleData(
            tireFLkPa = fl,
            tireFRkPa = fr,
            tireRLkPa = rl,
            tireRRkPa = rr
        )
        val message = data.toHlpJson()
        bleManager.writeData(message)
        addLogEntry("TX", message.trim())
    }

    /**
     * Nhận speed limit từ LogcatReader (đọc logcat InstrumentActivity).
     * Gửi qua BLE sang ESP32 dưới dạng speed data (tương thích Vietmap source).
     */
    fun onLogcatSpeedLimitUpdate(limit: Int) {
        if (limit > 0) {
            onAccessibilityUpdate(
                speedLimit = limit,
                currentSpeed = -1,
                roadName = "",
                source = "logcat"
            )
        }
    }

    /**
     * Nhận navigation data từ Google Maps accessibility parsing.
     * Gửi đầy đủ: direction, distance, road, time remaining, ETA.
     */
    fun onGoogleMapsNavUpdate(
        direction: String,
        distance: String,
        roadName: String,
        instruction: String,
        timeRemaining: String,
        totalDistance: String,
        eta: String
    ) {
        val json = buildString {
            append("{\"v\":1,\"t\":\"nav\"")
            if (direction.isNotBlank()) append(",\"dir\":\"$direction\"")
            if (distance.isNotBlank()) append(",\"dist\":\"${nfc(distance).replace("\"", "")}\"")
            if (roadName.isNotBlank()) {
                val escaped = nfc(roadName).replace("\"", "\\\"").take(60)
                append(",\"road\":\"$escaped\"")
            }
            if (instruction.isNotBlank()) {
                val escaped = nfc(instruction).replace("\"", "\\\"").take(80)
                append(",\"instruction\":\"$escaped\"")
            }
            if (timeRemaining.isNotBlank()) append(",\"time\":\"${nfc(timeRemaining).replace("\"", "")}\"")
            if (totalDistance.isNotBlank()) append(",\"total_dist\":\"${nfc(totalDistance).replace("\"", "")}\"")
            if (eta.isNotBlank()) append(",\"eta\":\"${nfc(eta).replace("\"", "")}\"")
            append("}\n")
        }
        bleManager.writeData(json)
        addLogEntry("TX", json.trim())
    }

    fun addLogEntry(direction: String, message: String) {
        val current = _messageLog.value.toMutableList()
        current.add(0, LogEntry(direction, message, System.currentTimeMillis()))
        if (current.size > 50) {
            _messageLog.value = current.take(50)
        } else {
            _messageLog.value = current
        }
    }

    // ─── Map stream control ─────────────────────────────────────────────────

    /**
     * Start streaming the screen capture to ESP32 over BLE.
     * Must call mapStreamManager.onPermissionResult() first from the Activity.
     */
    fun startMapStream() {
        mapStreamManager.startStreaming()
        addLogEntry("SYS", "Map streaming started (${mapStreamManager.fps} FPS)")
    }

    /**
     * Stop map streaming.
     */
    fun stopMapStream() {
        mapStreamManager.stopStreaming()
        addLogEntry("SYS", "Map streaming stopped")
    }

    // ─────────────────────────────────────────────────────────────────────────

    fun startForegroundService() {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java)
        try {
            startForegroundService(intent)
        } catch (e: IllegalStateException) {
            android.util.Log.e("CarNavApp", "Cannot start foreground service: ${e.message}")
            // Fallback: try startService (will work if app is in foreground)
            try { startService(intent) } catch (_: Exception) {}
        }
    }

    fun stopForegroundService() {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java)
        stopService(intent)
    }

    /**
     * Legacy: cập nhật notification text thủ công.
     * BleForegroundService giờ tự observe bleState và tự cập nhật notification.
     * Giữ lại cho trường hợp cần force-update status từ bên ngoài.
     */
    fun updateForegroundServiceStatus(status: String) {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java).apply {
            putExtra("status", status)
        }
        try { startService(intent) } catch (_: IllegalStateException) {}
    }
}

data class LogEntry(
    val direction: String,
    val message: String,
    val timestamp: Long
)
