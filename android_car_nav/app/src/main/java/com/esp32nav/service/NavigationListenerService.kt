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
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        when (sbn.packageName) {
            GOOGLE_MAPS_PACKAGE -> handleGoogleMaps(sbn)
            DatMapParser.DATMAP_PACKAGE -> handleDatMap(sbn)
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

        // Google Maps có 2 format tuỳ phone/car:
        //
        // Phone format:
        //   title: "1.7 km" (distance)
        //   text: "CT37 Đ. Vành Đai 3" (road name)
        //   subText: "36 min · 12 km · 10:20 AM ETA"
        //
        // Car/Android Auto format:
        //   title: "Đi về hướng tây" hoặc "Rẽ trái vào..." (instruction)
        //   text: "Mỹ Đình Pearl - Dự kiến 15:38" (destination + ETA)
        //   subText: "10 phút · 3,5 km · Dự kiến 15:38"

        var direction = ""
        var distance = ""
        var roadName = ""
        var instruction = ""
        var timeRemaining = ""
        var totalDistance = ""
        var eta = ""

        // Phân biệt title là distance hay instruction:
        // Distance patterns: "1.7 km", "350 m", "200 ft"
        val isDistanceTitle = Regex("""^\d+[.,]?\d*\s*(km|m|mi|ft)$""", RegexOption.IGNORE_CASE).matches(title.trim())

        if (isDistanceTitle) {
            // Phone format: title = distance
            distance = title.trim()
            roadName = text.trim()
        } else {
            // Car format: title = instruction text
            instruction = title.trim()
            direction = parseDirectionFromInstruction(instruction)

            // text thường là "Destination - Dự kiến HH:MM" hoặc road name
            // Không gán vào roadName vì đây là destination, không phải đường sẽ rẽ
        }

        // Parse subText: "10 phút · 3,5 km · Dự kiến 15:38"
        // hoặc: "36 min · 12 km · 10:20 AM ETA"
        if (subText.isNotBlank()) {
            val parts = subText.split("·", "•").map { it.trim() }
            for (part in parts) {
                when {
                    // Time remaining: "10 phút", "36 min", "1 h 20 min"
                    part.contains("phút") || part.contains("min") || part.contains("giờ") ||
                    part.contains("hr") || part.matches(Regex(""".*\d+\s*h\b.*""")) -> {
                        timeRemaining = part
                    }
                    // Distance: "3,5 km", "12 km", "350 m"
                    part.matches(Regex("""[\d.,]+\s*(km|m|mi)""")) -> {
                        totalDistance = part
                        // Nếu chưa có distance (car format), dùng total distance
                        if (distance.isBlank()) distance = part
                    }
                    // ETA: "Dự kiến 15:38", "10:20 AM ETA", "15:38"
                    part.contains("Dự kiến") || part.contains("ETA") ||
                    part.matches(Regex(""".*\d{1,2}:\d{2}.*""")) -> {
                        eta = part.replace("Dự kiến", "").replace("ETA", "").trim()
                    }
                }
            }
        }

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
