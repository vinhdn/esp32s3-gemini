package com.esp32nav.vhal

import android.content.Context
import android.util.Log
import com.esp32nav.model.VehicleData
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.lang.reflect.Method

/**
 * Đọc thông số xe qua Android Car API (VHAL) bằng reflection.
 *
 * Dùng reflection để không cần android.car.jar lúc compile.
 * Runtime trên thiết bị có Car Service sẽ hoạt động bình thường.
 * Trên thiết bị không có Car Service sẽ báo UNAVAILABLE.
 *
 * Vehicle Property IDs:
 * - PERF_VEHICLE_SPEED (0x11600207): m/s → km/h
 * - ENGINE_RPM (0x11600305)
 * - ENGINE_COOLANT_TEMP (0x11600301): °C
 * - ENGINE_OIL_TEMP (0x11600304): °C
 * - TIRE_PRESSURE (0x14e00306): kPa per wheel
 */
class VhalManager(private val context: Context) {

    companion object {
        private const val TAG = "VhalManager"
        private const val POLL_INTERVAL_MS = 500L

        // VehiclePropertyIds constants
        private const val PERF_VEHICLE_SPEED = 0x11600207
        private const val ENGINE_RPM = 0x11600305
        private const val ENGINE_COOLANT_TEMP = 0x11600301
        private const val ENGINE_OIL_TEMP = 0x11600304
        private const val TIRE_PRESSURE = 0x14e00306

        // VehicleAreaWheel
        private const val WHEEL_LEFT_FRONT = 0x1
        private const val WHEEL_RIGHT_FRONT = 0x2
        private const val WHEEL_LEFT_REAR = 0x4
        private const val WHEEL_RIGHT_REAR = 0x8
    }

    enum class VhalConnectionState {
        DISCONNECTED, CONNECTING, CONNECTED, UNAVAILABLE
    }

    private var carObject: Any? = null
    private var propertyManager: Any? = null
    private var getFloatPropertyMethod: Method? = null
    private var pollJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val _connectionState = MutableStateFlow(VhalConnectionState.DISCONNECTED)
    val connectionState: StateFlow<VhalConnectionState> = _connectionState.asStateFlow()

    private val _vehicleData = MutableStateFlow(VehicleData())
    val vehicleData: StateFlow<VehicleData> = _vehicleData.asStateFlow()

    fun connect() {
        if (_connectionState.value == VhalConnectionState.CONNECTED) return
        _connectionState.value = VhalConnectionState.CONNECTING

        scope.launch {
            try {
                // Thử load android.car.Car class bằng reflection
                val carClass = Class.forName("android.car.Car")
                val createCarMethod = carClass.getMethod("createCar", Context::class.java)
                carObject = createCarMethod.invoke(null, context)

                if (carObject == null) {
                    _connectionState.value = VhalConnectionState.UNAVAILABLE
                    return@launch
                }

                // Lấy CarPropertyManager
                val getCarManagerMethod = carClass.getMethod("getCarManager", String::class.java)
                propertyManager = getCarManagerMethod.invoke(carObject, "property")

                if (propertyManager == null) {
                    _connectionState.value = VhalConnectionState.UNAVAILABLE
                    return@launch
                }

                // Cache getFloatProperty method
                val pmClass = propertyManager!!.javaClass
                getFloatPropertyMethod = pmClass.getMethod("getFloatProperty", Int::class.javaPrimitiveType, Int::class.javaPrimitiveType)

                _connectionState.value = VhalConnectionState.CONNECTED
                Log.i(TAG, "Connected to Car Service via reflection")
                startPolling()

            } catch (e: ClassNotFoundException) {
                Log.w(TAG, "android.car.Car not available (not Automotive OS)")
                _connectionState.value = VhalConnectionState.UNAVAILABLE
            } catch (e: Exception) {
                Log.e(TAG, "Failed to connect to Car Service: ${e.message}")
                _connectionState.value = VhalConnectionState.UNAVAILABLE
            }
        }
    }

    private fun startPolling() {
        pollJob = scope.launch {
            while (isActive && _connectionState.value == VhalConnectionState.CONNECTED) {
                try {
                    val speed = readFloat(PERF_VEHICLE_SPEED, 0)
                    val rpm = readFloat(ENGINE_RPM, 0)
                    val coolant = readFloat(ENGINE_COOLANT_TEMP, 0)
                    val oil = readFloat(ENGINE_OIL_TEMP, 0)
                    val tireFL = readFloat(TIRE_PRESSURE, WHEEL_LEFT_FRONT)
                    val tireFR = readFloat(TIRE_PRESSURE, WHEEL_RIGHT_FRONT)
                    val tireRL = readFloat(TIRE_PRESSURE, WHEEL_LEFT_REAR)
                    val tireRR = readFloat(TIRE_PRESSURE, WHEEL_RIGHT_REAR)

                    _vehicleData.value = VehicleData(
                        speedKmh = speed?.let { (it * 3.6f).toInt() } ?: -1,
                        rpm = rpm?.toInt() ?: -1,
                        coolantTempC = coolant?.toInt() ?: -999,
                        oilTempC = oil?.toInt() ?: -999,
                        intakeTempC = -999,
                        tireFLkPa = tireFL?.toInt() ?: -1,
                        tireFRkPa = tireFR?.toInt() ?: -1,
                        tireRLkPa = tireRL?.toInt() ?: -1,
                        tireRRkPa = tireRR?.toInt() ?: -1
                    )
                } catch (e: Exception) {
                    Log.e(TAG, "Poll error: ${e.message}")
                    _connectionState.value = VhalConnectionState.UNAVAILABLE
                    break
                }
                delay(POLL_INTERVAL_MS)
            }
        }
    }

    private fun readFloat(propertyId: Int, areaId: Int): Float? {
        return try {
            getFloatPropertyMethod?.invoke(propertyManager, propertyId, areaId) as? Float
        } catch (e: Exception) {
            null
        }
    }

    fun disconnect() {
        pollJob?.cancel()
        pollJob = null
        try {
            carObject?.javaClass?.getMethod("disconnect")?.invoke(carObject)
        } catch (_: Exception) {}
        carObject = null
        propertyManager = null
        _connectionState.value = VhalConnectionState.DISCONNECTED
    }

    fun destroy() {
        disconnect()
        scope.cancel()
    }
}
