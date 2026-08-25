package com.esp32nav.obd

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.util.Log
import com.esp32nav.model.VehicleData
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID

/**
 * OBD-II manager kết nối qua Bluetooth Classic (SPP) tới ELM327 adapter.
 *
 * Flow: App → BT Classic → ELM327 → OBD-II port xe
 *
 * PIDs đọc:
 * - 0x0D: Vehicle speed (km/h)
 * - 0x0C: Engine RPM
 * - 0x05: Coolant temperature
 * - 0x0F: Intake air temperature
 * - 0x5C: Engine oil temperature (nếu xe hỗ trợ)
 *
 * Áp suất lốp (TPMS): Đa số xe đời mới có TPMS nhưng không expose qua OBD-II
 * standard PIDs. Cần xe hỗ trợ enhanced/manufacturer-specific PIDs.
 * Hiện tại sẽ trả -1 cho tire pressure.
 */
@SuppressLint("MissingPermission")
class ObdManager(private val context: Context) {

    companion object {
        private const val TAG = "ObdManager"
        // Standard SPP UUID cho Bluetooth serial
        private val SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        private const val ELM_TIMEOUT_MS = 3000L
        private const val POLL_INTERVAL_MS = 1000L
    }

    enum class ObdConnectionState {
        DISCONNECTED, CONNECTING, CONNECTED, ERROR
    }

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter

    private var socket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null
    private var pollJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val _connectionState = MutableStateFlow(ObdConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ObdConnectionState> = _connectionState.asStateFlow()

    private val _vehicleData = MutableStateFlow(VehicleData())
    val vehicleData: StateFlow<VehicleData> = _vehicleData.asStateFlow()

    private val _connectedDeviceName = MutableStateFlow("")
    val connectedDeviceName: StateFlow<String> = _connectedDeviceName.asStateFlow()

    /**
     * Lấy danh sách thiết bị Bluetooth Classic đã pair (để chọn ELM327).
     */
    fun getPairedDevices(): List<BluetoothDevice> {
        return try {
            bluetoothAdapter?.bondedDevices?.toList() ?: emptyList()
        } catch (e: SecurityException) {
            Log.e(TAG, "Cannot get paired devices: ${e.message}")
            emptyList()
        }
    }

    /**
     * Kết nối tới thiết bị ELM327 đã pair.
     */
    fun connect(device: BluetoothDevice) {
        scope.launch {
            try {
                _connectionState.value = ObdConnectionState.CONNECTING
                _connectedDeviceName.value = try { device.name ?: device.address } catch (_: SecurityException) { device.address }

                socket = device.createRfcommSocketToServiceRecord(SPP_UUID)

                // Timeout socket.connect() to avoid indefinite blocking
                withTimeout(15_000) {
                    socket?.connect()
                }

                inputStream = socket?.inputStream
                outputStream = socket?.outputStream

                // Khởi tạo ELM327
                if (initElm327()) {
                    _connectionState.value = ObdConnectionState.CONNECTED
                    startPolling()
                } else {
                    _connectionState.value = ObdConnectionState.ERROR
                    disconnect()
                }
            } catch (e: SecurityException) {
                Log.e(TAG, "Connect permission denied: ${e.message}")
                _connectionState.value = ObdConnectionState.ERROR
                disconnect()
            } catch (e: TimeoutCancellationException) {
                Log.e(TAG, "Connect timeout (15s)")
                _connectionState.value = ObdConnectionState.ERROR
                disconnect()
            } catch (e: IOException) {
                Log.e(TAG, "Connect failed: ${e.message}")
                _connectionState.value = ObdConnectionState.ERROR
                disconnect()
            }
        }
    }

    fun disconnect() {
        pollJob?.cancel()
        pollJob = null
        try {
            socket?.close()
        } catch (_: Exception) {}
        socket = null
        inputStream = null
        outputStream = null
        _connectionState.value = ObdConnectionState.DISCONNECTED
        _vehicleData.value = VehicleData()
    }

    private suspend fun initElm327(): Boolean {
        // Reset ELM327
        val resetResp = sendCommand("ATZ")
        Log.d(TAG, "ATZ response: $resetResp")
        delay(500)

        // Tắt echo
        sendCommand("ATE0")
        // Tắt linefeed
        sendCommand("ATL0")
        // Tắt spaces trong response
        sendCommand("ATS0")
        // Auto-detect protocol
        val protoResp = sendCommand("ATSP0")
        Log.d(TAG, "ATSP0 response: $protoResp")

        // Test kết nối bằng cách đọc voltage
        val voltResp = sendCommand("ATRV")
        Log.d(TAG, "Battery voltage: $voltResp")

        return voltResp.isNotBlank() && !voltResp.contains("ERROR", ignoreCase = true)
    }

    private fun startPolling() {
        pollJob = scope.launch {
            while (isActive && _connectionState.value == ObdConnectionState.CONNECTED) {
                try {
                    val speed = readPid("010D", ::parseSpeed)
                    val rpm = readPid("010C", ::parseRpm)
                    val coolant = readPid("0105", ::parseCoolantTemp)
                    val intake = readPid("010F", ::parseIntakeTemp)
                    val oil = readPid("015C", ::parseOilTemp)

                    _vehicleData.value = VehicleData(
                        speedKmh = speed ?: -1,
                        rpm = rpm ?: -1,
                        coolantTempC = coolant ?: -999,
                        intakeTempC = intake ?: -999,
                        oilTempC = oil ?: -999,
                        // TPMS: không support qua standard OBD-II PIDs
                        tireFLkPa = -1,
                        tireFRkPa = -1,
                        tireRLkPa = -1,
                        tireRRkPa = -1
                    )
                } catch (e: IOException) {
                    Log.e(TAG, "Polling error: ${e.message}")
                    _connectionState.value = ObdConnectionState.ERROR
                    break
                }
                delay(POLL_INTERVAL_MS)
            }
        }
    }

    private suspend fun <T> readPid(command: String, parser: (String) -> T?): T? {
        val response = sendCommand(command)
        if (response.contains("NO DATA") || response.contains("ERROR")) {
            return null
        }
        return parser(response)
    }

    private suspend fun sendCommand(command: String): String {
        val os = outputStream ?: return ""
        val ins = inputStream ?: return ""

        return withContext(Dispatchers.IO) {
            try {
                // Flush input buffer
                while (ins.available() > 0) {
                    ins.read()
                }

                os.write("$command\r".toByteArray())
                os.flush()

                // Read response until '>' prompt
                val buffer = StringBuilder()
                val startTime = System.currentTimeMillis()
                while (System.currentTimeMillis() - startTime < ELM_TIMEOUT_MS) {
                    if (ins.available() > 0) {
                        val c = ins.read().toChar()
                        if (c == '>') break
                        buffer.append(c)
                    } else {
                        delay(10)
                    }
                }
                buffer.toString().trim().replace("\r", "").replace("\n", "")
            } catch (e: IOException) {
                Log.e(TAG, "sendCommand error: ${e.message}")
                ""
            }
        }
    }

    // --- PID Parsers ---

    private fun parseSpeed(response: String): Int? {
        // Response format: "410D XX" where XX is speed in km/h
        val hex = extractDataBytes(response, "410D") ?: return null
        if (hex.length < 2) return null
        return hex.substring(0, 2).toIntOrNull(16)
    }

    private fun parseRpm(response: String): Int? {
        // Response format: "410C XXXX" where value = ((A*256)+B)/4
        val hex = extractDataBytes(response, "410C") ?: return null
        if (hex.length < 4) return null
        val a = hex.substring(0, 2).toIntOrNull(16) ?: return null
        val b = hex.substring(2, 4).toIntOrNull(16) ?: return null
        return (a * 256 + b) / 4
    }

    private fun parseCoolantTemp(response: String): Int? {
        // Response format: "4105 XX" where temp = X - 40
        val hex = extractDataBytes(response, "4105") ?: return null
        if (hex.length < 2) return null
        val value = hex.substring(0, 2).toIntOrNull(16) ?: return null
        return value - 40
    }

    private fun parseIntakeTemp(response: String): Int? {
        // Response format: "410F XX" where temp = X - 40
        val hex = extractDataBytes(response, "410F") ?: return null
        if (hex.length < 2) return null
        val value = hex.substring(0, 2).toIntOrNull(16) ?: return null
        return value - 40
    }

    private fun parseOilTemp(response: String): Int? {
        // Response format: "415C XX" where temp = X - 40
        val hex = extractDataBytes(response, "415C") ?: return null
        if (hex.length < 2) return null
        val value = hex.substring(0, 2).toIntOrNull(16) ?: return null
        return value - 40
    }

    /**
     * Trích xuất data bytes từ OBD response.
     * VD: response = "410D3C" → prefix="410D" → return "3C"
     */
    private fun extractDataBytes(response: String, prefix: String): String? {
        // Remove spaces
        val clean = response.replace(" ", "").uppercase()
        val idx = clean.indexOf(prefix.uppercase())
        if (idx < 0) return null
        return clean.substring(idx + prefix.length)
    }

    fun destroy() {
        pollJob?.cancel()
        scope.cancel()
        try { socket?.close() } catch (_: Exception) {}
    }
}
