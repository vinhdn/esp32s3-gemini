package com.esp32nav.parser

import android.util.Log

/**
 * Parse thông tin tốc độ giới hạn và cảnh báo giao thông từ app DatMap.
 *
 * DatMap (com.datmap.app) cung cấp:
 * - Cảnh báo tốc độ giới hạn trên đường hiện tại
 * - Cảnh báo camera tốc độ
 * - Cảnh báo camera giám sát giao thông
 * - Cảnh báo đèn đỏ
 * - Cảnh báo khu dân cư
 * - Cảnh báo trạm thu phí
 *
 * DatMap expose thông tin qua:
 * 1. Notification: cảnh báo camera/tốc độ trong notification tray
 * 2. Floating bubble/overlay: hiển thị tốc độ giới hạn real-time trên các app khác
 *
 * Cách đọc:
 * - NotificationListenerService: đọc notification text
 * - AccessibilityService: đọc text content trên bubble overlay window
 */
object DatMapParser {

    private const val TAG = "DatMapParser"
    const val DATMAP_PACKAGE = "com.datmap.app"

    data class DatMapData(
        val speedLimit: Int = -1,        // Tốc độ giới hạn (km/h), -1 = chưa có
        val cameraDistance: Int = -1,     // Khoảng cách tới camera (m), -1 = không có
        val cameraType: String = "",     // "speed", "red_light", "surveillance", ""
        val alertMessage: String = ""    // Thông báo gốc
    )

    /**
     * Parse notification text từ DatMap.
     * DatMap gửi notifications dạng:
     * - "Giới hạn tốc độ: 60 km/h"
     * - "Camera tốc độ cách 500m - Giới hạn 80 km/h"
     * - "Camera giám sát cách 200m"
     * - "Camera đèn đỏ cách 100m"
     * - "Khu dân cư - Giới hạn 50 km/h"
     * - "Trạm thu phí cách 1km"
     */
    fun parseNotification(title: String?, text: String?): DatMapData? {
        val fullText = listOfNotNull(title, text).joinToString(" ")
        if (fullText.isBlank()) return null

        Log.d(TAG, "Parse DatMap notification: title=$title text=$text")

        val speedLimit = extractSpeedLimit(fullText)
        val cameraDistance = extractCameraDistance(fullText)
        val cameraType = detectCameraType(fullText)

        if (speedLimit < 0 && cameraDistance < 0 && cameraType.isBlank()) {
            return null
        }

        return DatMapData(
            speedLimit = speedLimit,
            cameraDistance = cameraDistance,
            cameraType = cameraType,
            alertMessage = fullText.trim()
        )
    }

    /**
     * Parse text content từ DatMap floating bubble/overlay.
     * Bubble thường hiển thị tốc độ giới hạn hiện tại dạng: "60" hoặc "80"
     * Hoặc dạng: "60 km/h" hoặc icon + số
     */
    fun parseBubbleText(text: String): DatMapData? {
        if (text.isBlank()) return null

        Log.d(TAG, "Parse DatMap bubble: $text")

        // Thử parse số đơn thuần (bubble chỉ hiện số tốc độ giới hạn)
        val numberOnly = text.trim().replace(Regex("[^0-9]"), "")
        if (numberOnly.isNotEmpty()) {
            val value = numberOnly.toIntOrNull()
            if (value != null && value in 5..150) {
                return DatMapData(speedLimit = value)
            }
        }

        // Parse full text
        val speedLimit = extractSpeedLimit(text)
        val cameraDistance = extractCameraDistance(text)
        val cameraType = detectCameraType(text)

        if (speedLimit < 0 && cameraDistance < 0) return null

        return DatMapData(
            speedLimit = speedLimit,
            cameraDistance = cameraDistance,
            cameraType = cameraType,
            alertMessage = text.trim()
        )
    }

    /**
     * Trích xuất tốc độ giới hạn từ text.
     * Patterns: "60 km/h", "Giới hạn 80", "tốc độ: 60", "limit 60"
     */
    private fun extractSpeedLimit(text: String): Int {
        val patterns = listOf(
            // "Giới hạn tốc độ: 60 km/h" hoặc "Giới hạn 60"
            Regex("""[Gg]iới\s*hạn[^0-9]*(\d{2,3})\s*(?:km/?h)?"""),
            // "tốc độ 60 km/h" hoặc "speed limit 60"
            Regex("""[Tt]ốc\s*độ[^0-9]*(\d{2,3})\s*(?:km/?h)?"""),
            // "limit 60" (English fallback)
            Regex("""[Ll]imit[^0-9]*(\d{2,3})\s*(?:km/?h)?"""),
            // "60 km/h" standalone
            Regex("""(\d{2,3})\s*km/?h"""),
            // Trong ngữ cảnh khu dân cư hoặc cao tốc
            Regex("""[Kk]hu\s*dân\s*cư[^0-9]*(\d{2,3})"""),
        )

        for (pattern in patterns) {
            val match = pattern.find(text)
            if (match != null) {
                val value = match.groupValues[1].toIntOrNull()
                if (value != null && value in 5..200) {
                    return value
                }
            }
        }
        return -1
    }

    /**
     * Trích xuất khoảng cách tới camera.
     * Patterns: "cách 500m", "cách 1.2km", "500 m ahead"
     */
    private fun extractCameraDistance(text: String): Int {
        val patterns = listOf(
            // "cách 500m" hoặc "cách 1.5km"
            Regex("""[Cc]ách\s*(\d+[.,]?\d*)\s*(km|m)"""),
            // "500m" standalone khi context rõ là camera
            Regex("""(\d+[.,]?\d*)\s*(km|m)\s*(?:phía|nữa|ahead)?"""),
        )

        for (pattern in patterns) {
            val match = pattern.find(text)
            if (match != null) {
                val value = match.groupValues[1].replace(",", ".").toFloatOrNull() ?: continue
                val unit = match.groupValues[2].lowercase()
                val meters = when (unit) {
                    "km" -> (value * 1000).toInt()
                    else -> value.toInt()
                }
                if (meters in 1..50000) {
                    return meters
                }
            }
        }
        return -1
    }

    /**
     * Detect loại camera từ text.
     */
    private fun detectCameraType(text: String): String {
        val lower = text.lowercase()
        return when {
            lower.contains("đèn đỏ") || lower.contains("red light") -> "red_light"
            lower.contains("tốc độ") || lower.contains("speed") -> "speed"
            lower.contains("giám sát") || lower.contains("surveillance") -> "surveillance"
            lower.contains("camera") -> "speed" // default camera type
            else -> ""
        }
    }
}
