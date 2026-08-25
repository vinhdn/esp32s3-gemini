package com.esp32nav.service

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.esp32nav.CarNavApplication
import com.esp32nav.parser.DatMapParser
import com.esp32nav.parser.GoogleMapsParser

class NavigationListenerService : NotificationListenerService() {

    companion object {
        private const val TAG = "NavListener"
        private const val GOOGLE_MAPS_PACKAGE = "com.google.android.apps.maps"
        private const val VIETMAP_PACKAGE = "vn.vietmap.live"
        private val VIETMAP_PACKAGES = arrayOf(
            "vn.vietmap.live",
            "vn.vietmap.live.v2",
            "vn.vietmap.vietmaplive",
            "vn.vietmap.vietmap_map",
            "com.vietmap.live",
            "vn.vietmap.navi",
            "com.vietmap.navigator"
        )
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        when {
            sbn.packageName == GOOGLE_MAPS_PACKAGE -> handleGoogleMaps(sbn)
            sbn.packageName == DatMapParser.DATMAP_PACKAGE -> handleDatMap(sbn)
            VIETMAP_PACKAGES.contains(sbn.packageName) -> handleVietmap(sbn)
        }
    }

    /**
     * Handle Vietmap Live notification.
     * Vietmap có thể gửi notification cảnh báo tốc độ/camera khi navigate.
     * Format notification có thể:
     * - title: "Cảnh báo tốc độ" / "Camera phía trước"
     * - text: "Giới hạn 60 km/h" / "Camera 500m phía trước, giới hạn 80km/h"
     */
    private fun handleVietmap(sbn: StatusBarNotification) {
        val notification = sbn.notification ?: return
        val extras = notification.extras ?: return

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""

        // Skip foreground service notifications (generic messages)
        if (title.contains("VIETMAP LIVE", ignoreCase = true) && 
            (text.contains("Trợ lý lái xe") || text.contains("Định vị"))) {
            return
        }

        Log.d(TAG, "Vietmap notification - title: $title, text: $text")

        // Parse speed limit from notification
        val combined = "$title $text"
        var speedLimit = 0
        var cameraDistance = 0

        // Pattern: "60 km/h", "Giới hạn 80", "tốc độ 60"
        val limitMatch = Regex("""(?:giới hạn|tốc độ|limit)\s*:?\s*(\d{2,3})\s*(?:km/?h)?""", RegexOption.IGNORE_CASE).find(combined)
        if (limitMatch != null) {
            speedLimit = limitMatch.groupValues[1].toIntOrNull() ?: 0
        } else {
            // Fallback: find "XX km/h" pattern
            val kmhMatch = Regex("""(\d{2,3})\s*km/?h""", RegexOption.IGNORE_CASE).find(combined)
            if (kmhMatch != null) {
                speedLimit = kmhMatch.groupValues[1].toIntOrNull() ?: 0
            }
        }

        // Camera distance: "500m phía trước", "camera 300 m"
        val distMatch = Regex("""(\d+)\s*m""", RegexOption.IGNORE_CASE).find(combined)
        if (distMatch != null && combined.contains("camera", ignoreCase = true)) {
            cameraDistance = distMatch.groupValues[1].toIntOrNull() ?: 0
        }

        if (speedLimit > 0 || cameraDistance > 0) {
            Log.i(TAG, "Vietmap: speedLimit=$speedLimit cameraDistance=$cameraDistance")
            val app = application as? CarNavApplication ?: return
            app.onAccessibilityUpdate(
                speedLimit = speedLimit,
                currentSpeed = -1,
                roadName = "",
                source = "vietmap_notif"
            )
        }
    }

    private fun handleGoogleMaps(sbn: StatusBarNotification) {
        val notification = sbn.notification ?: return
        if (notification.category != Notification.CATEGORY_NAVIGATION) return

        val extras = notification.extras ?: return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
        val subText = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString() ?: ""

        Log.d(TAG, "Google Maps nav - title: $title, text: $text, subText: $subText")

        // Google Maps có 3 format tuỳ thiết bị:
        //
        // Format A - Phone:
        //   title: "1.7 km" (distance only)
        //   text: "CT37 Đ. Vành Đai 3" (road name)
        //   subText: "36 min · 12 km · 10:20 AM ETA"
        //
        // Format B - Car/Android Auto:
        //   title: "Đi về hướng tây" hoặc "Rẽ trái vào..." (instruction only)
        //   text: "Mỹ Đình Pearl - Dự kiến 15:38" (destination + ETA)
        //   subText: "10 phút · 3,5 km · Dự kiến 15:38"
        //
        // Format C - Car head unit (observed on device):
        //   title: "270 m - về hướng Mỹ Đình" (distance + instruction combined with " - ")
        //   text: "VỊT 34 - Dự kiến 21:12" (destination + ETA)
        //   subText: "11 phút · 4,5 km · Dự kiến 21:12"

        var direction = ""
        var distance = ""
        var roadName = ""
        var instruction = ""
        var timeRemaining = ""
        var totalDistance = ""
        var eta = ""

        // Distance pattern at the start: "270 m", "1.7 km", "1,5 km", "350 m"
        val distanceStartRegex = Regex("""^([\d.,]+\s*(?:km|m|mi|ft))""", RegexOption.IGNORE_CASE)

        // Check title format
        val titleTrimmed = title.trim()
        val distanceStartMatch = distanceStartRegex.find(titleTrimmed)

        when {
            // Format C: title starts with distance + " - " + instruction
            // e.g. "270 m - về hướng Mỹ Đình"
            distanceStartMatch != null && titleTrimmed.contains(" - ") -> {
                distance = distanceStartMatch.groupValues[1].trim()
                val afterDistance = titleTrimmed.substring(distanceStartMatch.range.last + 1).trim()
                instruction = afterDistance.removePrefix("-").removePrefix("–").trim()
                direction = parseDirectionFromInstruction(instruction)

                // text = "VỊT 34 - Dự kiến 21:12" → extract road/destination
                if (text.isNotBlank()) {
                    val etaInText = Regex("""[–-]\s*Dự kiến\s*(\d{1,2}:\d{2})""").find(text)
                    if (etaInText != null) {
                        eta = etaInText.groupValues[1]
                        // Đây là destination, không phải road name
                    }
                }

                // Road name lấy từ instruction (e.g. "về hướng Mỹ Đình" → road = instruction)
                roadName = instruction
            }

            // Format A: title is only distance "1.7 km", "350 m"
            distanceStartMatch != null && distanceStartMatch.value.trim() == titleTrimmed -> {
                distance = titleTrimmed
                roadName = text.trim()
            }

            // Format B: title is instruction text only
            else -> {
                instruction = titleTrimmed
                direction = parseDirectionFromInstruction(instruction)

                // Thử extract road name từ instruction: "Rẽ trái vào Nguyễn Trãi"
                val roadFromInstruction = extractRoadFromInstruction(instruction)
                if (roadFromInstruction.isNotBlank()) {
                    roadName = roadFromInstruction
                }
            }
        }

        // Parse subText: "11 phút · 4,5 km · Dự kiến 21:12"
        if (subText.isNotBlank()) {
            val parts = subText.split("·", "•").map { it.trim() }
            for (part in parts) {
                when {
                    // Time remaining: "10 phút", "36 min", "1 giờ 20 phút", "1 h 20 min"
                    part.contains("phút") || part.contains("min") || part.contains("giờ") ||
                    part.contains("hr") || part.matches(Regex(""".*\d+\s*h\b.*""")) -> {
                        timeRemaining = part
                    }
                    // Distance: "3,5 km", "12 km", "350 m"
                    part.matches(Regex("""[\d.,]+\s*(km|m|mi)""")) -> {
                        totalDistance = part
                    }
                    // ETA: "Dự kiến 15:38", "10:20 AM ETA", "15:38"
                    part.contains("Dự kiến") || part.contains("ETA") ||
                    part.matches(Regex(""".*\d{1,2}:\d{2}.*""")) -> {
                        val etaParsed = part.replace("Dự kiến", "").replace("ETA", "").trim()
                        if (etaParsed.isNotBlank()) eta = etaParsed
                    }
                }
            }
        }

        // Nếu vẫn chưa có direction, thử infer từ distance + instruction text
        if (direction.isBlank() && instruction.isBlank() && distance.isNotBlank()) {
            direction = "straight" // Đang trên đường thẳng, chưa có lượt rẽ
        }

        Log.i(TAG, "Parsed: dir=$direction dist=$distance road=$roadName inst=$instruction time=$timeRemaining total=$totalDistance eta=$eta")

        val app = application as? CarNavApplication ?: return
        app.onGoogleMapsNavUpdate(
            direction = direction,
            distance = distance,
            roadName = roadName,
            instruction = instruction,
            timeRemaining = timeRemaining,
            totalDistance = totalDistance,
            eta = eta
        )
    }

    /**
     * Extract road name từ instruction.
     * E.g. "Rẽ trái vào Nguyễn Trãi" -> "Nguyễn Trãi"
     *      "Turn right onto Main St" -> "Main St"
     */
    private fun extractRoadFromInstruction(instruction: String): String {
        val patterns = listOf(
            // Vietnamese: "vào X", "sang X", "theo X", "ra X"
            Regex("""(?:vào|sang|theo|ra|trên)\s+(.+)""", RegexOption.IGNORE_CASE),
            // English: "onto X", "on X", "toward X"
            Regex("""(?:onto|on|toward|towards)\s+(.+)""", RegexOption.IGNORE_CASE),
        )
        for (pattern in patterns) {
            val match = pattern.find(instruction)
            if (match != null) {
                return match.groupValues[1].trim().trimEnd('.', ',', ';')
            }
        }
        return ""
    }

    /**
     * Parse direction từ instruction text (tiếng Việt + English).
     */
    private fun parseDirectionFromInstruction(instruction: String): String {
        val lower = instruction.lowercase()
        return when {
            // Vietnamese
            lower.contains("quay đầu") -> "u_turn"
            lower.contains("rẽ trái") || lower.contains("re trái") || lower.contains("quẹo trái") -> "turn_left"
            lower.contains("rẽ phải") || lower.contains("re phải") || lower.contains("quẹo phải") -> "turn_right"
            lower.contains("đi thẳng") || lower.contains("đi tiếp") || lower.contains("tiếp tục") -> "straight"
            lower.contains("đi về hướng") || lower.contains("về hướng") -> "straight"
            lower.contains("chếch trái") || lower.contains("lệch trái") || lower.contains("giữ bên trái") -> "slight_left"
            lower.contains("chếch phải") || lower.contains("lệch phải") || lower.contains("giữ bên phải") -> "slight_right"
            lower.contains("vòng xoay") || lower.contains("bùng binh") -> "roundabout"
            lower.contains("đến nơi") || lower.contains("điểm đến") -> "arrive"
            lower.contains("nhập") || lower.contains("nhập làn") -> "merge"
            // English
            lower.contains("u-turn") -> "u_turn"
            lower.contains("turn left") || lower.contains("left") -> "turn_left"
            lower.contains("turn right") || lower.contains("right") -> "turn_right"
            lower.contains("keep left") || lower.contains("bear left") -> "slight_left"
            lower.contains("keep right") || lower.contains("bear right") -> "slight_right"
            lower.contains("straight") || lower.contains("head") || lower.contains("continue") -> "straight"
            lower.contains("roundabout") -> "roundabout"
            lower.contains("arrive") || lower.contains("destination") -> "arrive"
            lower.contains("merge") -> "merge"
            else -> "straight"
        }
    }

    private fun handleDatMap(sbn: StatusBarNotification) {
        val notification = sbn.notification ?: return
        val extras = notification.extras ?: return

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()

        Log.d(TAG, "DatMap notification - title: $title, text: $text")

        val datMapData = DatMapParser.parseNotification(title, text) ?: return

        val app = application as? CarNavApplication ?: return
        app.onDatMapUpdate(datMapData)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.packageName != GOOGLE_MAPS_PACKAGE) return
        val notification = sbn.notification ?: return
        if (notification.category != Notification.CATEGORY_NAVIGATION) return

        Log.d(TAG, "Navigation notification removed")
        val app = application as? CarNavApplication ?: return
        app.onNavigationCleared()
    }
}
