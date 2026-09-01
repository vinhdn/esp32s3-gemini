package com.esp32nav.weather

import android.annotation.SuppressLint
import android.content.Context
import android.location.LocationManager
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

private const val TAG = "WeatherManager"

/**
 * Lấy thời tiết (Open-Meteo — free, không cần API key) để hiển thị trên
 * board thay cho vòng tròn biển báo khi không có dữ liệu tốc độ giới hạn
 * (xem VietmapAccessibilityService.kt/VmsxFrame.kt). Toạ độ lấy từ vị trí
 * biết gần nhất (ACCESS_FINE/COARSE_LOCATION đã có sẵn cho việc quét BLE).
 */
@SuppressLint("MissingPermission")
class WeatherManager(private val context: Context) {

    companion object {
        // Thời tiết không cần realtime — 15 phút/lần là đủ, né spam API free.
        private const val FETCH_INTERVAL_MS = 15 * 60 * 1000L

        const val CONDITION_CLEAR = 0
        const val CONDITION_CLOUDY = 1
        const val CONDITION_RAIN = 2
        const val CONDITION_STORM = 3
        const val CONDITION_SNOW = 4

        // Rut gon WMO weather code (Open-Meteo) ve 5 nhom hien thi duoc bang
        // 1 icon mau don gian tren board (xem ui_screens.c).
        private fun wmoToCondition(code: Int): Int = when (code) {
            0, 1 -> CONDITION_CLEAR
            2, 3, 45, 48 -> CONDITION_CLOUDY
            51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82 -> CONDITION_RAIN
            95, 96, 99 -> CONDITION_STORM
            71, 73, 75, 77, 85, 86 -> CONDITION_SNOW
            else -> CONDITION_CLOUDY
        }
    }

    @Volatile
    var currentTempC: Int? = null
        private set

    @Volatile
    var currentCondition: Int? = null
        private set

    @Volatile
    var tomorrowTempC: Int? = null
        private set

    @Volatile
    var tomorrowCondition: Int? = null
        private set

    private var lastFetchAt = 0L

    @Volatile
    private var fetching = false

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    /**
     * Gọi thường xuyên (mỗi lần đọc bong bóng cũng được, vd trong
     * parseBubbleWidget) — tự throttle bên trong theo FETCH_INTERVAL_MS,
     * không chặn caller (chạy nền). Kết quả đọc qua currentTempC/... ở lần
     * gọi VMSX kế tiếp sau khi fetch xong.
     */
    fun maybeFetch() {
        val now = System.currentTimeMillis()
        if (fetching || now - lastFetchAt < FETCH_INTERVAL_MS) return
        val location = lastKnownLocation() ?: return
        fetching = true
        lastFetchAt = now
        scope.launch {
            try {
                fetch(location.first, location.second)
            } catch (e: Exception) {
                Log.w(TAG, "fetch thời tiết lỗi: ${e.message}")
            } finally {
                fetching = false
            }
        }
    }

    private fun lastKnownLocation(): Pair<Double, Double>? {
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return null
        val providers = listOf(
            LocationManager.GPS_PROVIDER,
            LocationManager.NETWORK_PROVIDER,
            LocationManager.PASSIVE_PROVIDER,
        )
        for (p in providers) {
            try {
                val loc = lm.getLastKnownLocation(p) ?: continue
                return loc.latitude to loc.longitude
            } catch (_: Exception) {
                // Provider không tồn tại/không có quyền — thử provider khác.
            }
        }
        return null
    }

    private fun fetch(lat: Double, lon: Double) {
        val url = "https://api.open-meteo.com/v1/forecast" +
            "?latitude=$lat&longitude=$lon" +
            "&current=temperature_2m,weather_code" +
            "&daily=weather_code,temperature_2m_max" +
            "&timezone=auto&forecast_days=2"
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 8000
        conn.readTimeout = 8000
        conn.requestMethod = "GET"
        try {
            val code = conn.responseCode
            if (code != 200) {
                Log.w(TAG, "weather API trả về mã $code")
                return
            }
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            val json = JSONObject(body)

            json.optJSONObject("current")?.let { current ->
                if (current.has("temperature_2m")) {
                    currentTempC = Math.round(current.optDouble("temperature_2m")).toInt()
                }
                if (current.has("weather_code")) {
                    currentCondition = wmoToCondition(current.optInt("weather_code"))
                }
            }

            json.optJSONObject("daily")?.let { daily ->
                // Index 0 = hôm nay, 1 = ngày mai.
                val codes = daily.optJSONArray("weather_code")
                if (codes != null && codes.length() > 1) {
                    tomorrowCondition = wmoToCondition(codes.getInt(1))
                }
                val maxTemps = daily.optJSONArray("temperature_2m_max")
                if (maxTemps != null && maxTemps.length() > 1) {
                    tomorrowTempC = Math.round(maxTemps.getDouble(1)).toInt()
                }
            }

            Log.i(TAG, "🌤️ thời tiết: hôm nay ${currentTempC}°C cond=$currentCondition, " +
                "ngày mai ${tomorrowTempC}°C cond=$tomorrowCondition")
        } finally {
            conn.disconnect()
        }
    }
}
