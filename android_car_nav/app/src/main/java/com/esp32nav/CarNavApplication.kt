package com.esp32nav

import android.app.Application
import android.content.Intent
import com.esp32nav.ble.BleManager
import com.esp32nav.model.NavigationData
import com.esp32nav.model.VehicleData
import com.esp32nav.obd.ObdManager
import com.esp32nav.parser.DatMapParser
import com.esp32nav.vhal.VhalManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class CarNavApplication : Application() {

    lateinit var bleManager: BleManager
        private set

    lateinit var obdManager: ObdManager
        private set

    lateinit var vhalManager: VhalManager
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

        // Tự động bắt đầu scan BLE ngay khi app khởi tạo
        bleManager.setAutoReconnect(true)
        scope.launch {
            // Delay nhỏ cho hệ thống ổn định sau boot
            kotlinx.coroutines.delay(2000)
            startForegroundService()
            bleManager.startScan()
        }

        // Thử kết nối VHAL (sẽ thành công trên Android Automotive)
        vhalManager.connect()

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
                val escaped = roadName.replace("\"", "\\\"").replace("\n", " ").take(60)
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
            if (distance.isNotBlank()) append(",\"dist\":\"${distance.replace("\"", "")}\"")
            if (roadName.isNotBlank()) {
                val escaped = roadName.replace("\"", "\\\"").take(60)
                append(",\"road\":\"$escaped\"")
            }
            if (instruction.isNotBlank()) {
                val escaped = instruction.replace("\"", "\\\"").take(80)
                append(",\"instruction\":\"$escaped\"")
            }
            if (timeRemaining.isNotBlank()) append(",\"time\":\"${timeRemaining.replace("\"", "")}\"")
            if (totalDistance.isNotBlank()) append(",\"total_dist\":\"${totalDistance.replace("\"", "")}\"")
            if (eta.isNotBlank()) append(",\"eta\":\"${eta.replace("\"", "")}\"")
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

    fun startForegroundService() {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java)
        startForegroundService(intent)
    }

    fun stopForegroundService() {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java)
        stopService(intent)
    }

    fun updateForegroundServiceStatus(status: String) {
        val intent = Intent(this, com.esp32nav.service.BleForegroundService::class.java).apply {
            putExtra("status", status)
        }
        startService(intent)
    }
}

data class LogEntry(
    val direction: String,
    val message: String,
    val timestamp: Long
)
