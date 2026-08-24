package com.esp32nav.service

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.AccessibilityServiceInfo
import android.util.Log
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import com.esp32nav.CarNavApplication

/**
 * Accessibility Service đọc thông số từ Vietmap Live + DatMap.
 *
 * Vietmap Live hiển thị trên accessibility node tree:
 * - contentDescription = "{tốc_độ_hiện_tại}\nkm/h\n{tốc_độ_giới_hạn}"
 * - contentDescription = "{tên_đường}\n{quận}, {thành_phố}"
 *
 * DatMap hiển thị tốc độ giới hạn trên bubble overlay.
 *
 * Dữ liệu được gửi qua BLE sang ESP32 board.
 */
class VietmapAccessibilityService : AccessibilityService() {

    companion object {
        private const val TAG = "VietmapAccessibility"

        // Packages to monitor
        val MONITORED_PACKAGES = arrayOf(
            "vn.vietmap.live",
            "vn.vietmap.vietmaplive",
            "com.vietmap.live",
            "vn.vietmap.navi",
            "com.vietmap.navigator",
            "vn.vietmap.maps",
            "com.datmap.app",
            "com.google.android.apps.maps"
        )

        // Singleton state
        @Volatile var currentSpeedLimit: Int = 0
        @Volatile var currentSpeed: Int = 0
        @Volatile var currentRoadName: String = ""
        @Volatile var isNavigating: Boolean = false
        @Volatile var lastUpdateTime: Long = 0
        @Volatile var isServiceRunning: Boolean = false
    }

    private var lastDebugLog = 0L

    override fun onServiceConnected() {
        super.onServiceConnected()
        isServiceRunning = true
        Log.i(TAG, "✅ Accessibility Service connected - monitoring Vietmap/DatMap/Maps")

        val info = serviceInfo ?: AccessibilityServiceInfo()
        info.eventTypes = AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED or
                AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED
        info.feedbackType = AccessibilityServiceInfo.FEEDBACK_GENERIC
        info.flags = AccessibilityServiceInfo.FLAG_REPORT_VIEW_IDS or
                AccessibilityServiceInfo.FLAG_INCLUDE_NOT_IMPORTANT_VIEWS or
                AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
        info.notificationTimeout = 300
        info.packageNames = MONITORED_PACKAGES
        serviceInfo = info
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event == null) return
        val packageName = event.packageName?.toString() ?: return
        if (!MONITORED_PACKAGES.contains(packageName)) return

        isNavigating = true

        when (event.eventType) {
            AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED,
            AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED -> {
                processWindowContent(packageName)
            }
        }
    }

    override fun onInterrupt() {
        Log.w(TAG, "Accessibility Service interrupted")
        isServiceRunning = false
    }

    override fun onDestroy() {
        super.onDestroy()
        isServiceRunning = false
        Log.i(TAG, "Accessibility Service destroyed")
    }

    private fun processWindowContent(packageName: String) {
        val rootNode = rootInActiveWindow ?: return

        try {
            val allTexts = mutableListOf<NodeTextInfo>()
            collectAllTexts(rootNode, allTexts, 0)

            when {
                packageName.contains("vietmap") -> parseVietmapData(allTexts)
                packageName == "com.datmap.app" -> parseDatMapData(allTexts)
                packageName == "com.google.android.apps.maps" -> parseGoogleMapsData(allTexts)
            }

            // Debug log every 10 seconds
            if (System.currentTimeMillis() - lastDebugLog > 10000) {
                lastDebugLog = System.currentTimeMillis()
                Log.d(TAG, "=== [$packageName] ${allTexts.size} nodes ===")
                allTexts.take(20).forEach { info ->
                    if (info.text.isNotBlank() || info.contentDescription.isNotBlank()) {
                        Log.d(TAG, "  [${info.viewId}] text='${info.text}' desc='${info.contentDescription}'")
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error processing window: ${e.message}")
        } finally {
            rootNode.recycle()
        }
    }

    /**
     * Parse Vietmap Live node tree.
     * Format: contentDescription = "{speed}\nkm/h\n{limit}"
     *         contentDescription = "{road_name}\n{district}, {city}"
     */
    private fun parseVietmapData(texts: List<NodeTextInfo>) {
        var foundSpeedLimit = 0
        var foundCurrentSpeed = -1
        var foundRoadName = ""

        for (info in texts) {
            val desc = info.contentDescription.trim()
            val text = info.text.trim()
            val combined = desc.ifBlank { text }

            if (combined.isBlank()) continue

            // Format: "{speed}\nkm/h\n{limit}"
            if (combined.contains("km/h", ignoreCase = true)) {
                val lines = combined.split("\n").map { it.trim() }
                if (lines.size >= 3) {
                    val speed = lines[0].toIntOrNull()
                    val limit = lines[2].toIntOrNull()
                    if (speed != null && speed in 0..300) foundCurrentSpeed = speed
                    if (limit != null && limit in 5..200) foundSpeedLimit = limit
                } else if (lines.size == 2) {
                    val speed = lines[0].toIntOrNull()
                    if (speed != null && speed in 0..300) foundCurrentSpeed = speed
                }
            }
            // Road name: doesn't contain km/h, has meaningful text
            else if (combined.length > 3 && !combined.all { it.isDigit() }) {
                val lines = combined.split("\n").map { it.trim() }
                val candidate = lines.firstOrNull() ?: ""
                // Avoid picking up random UI elements
                if (candidate.length in 4..100 && !candidate.contains("km/h")) {
                    foundRoadName = candidate
                }
            }

            // Fallback: view ID based detection
            val viewId = info.viewId.lowercase()
            if (viewId.contains("speed_limit") || viewId.contains("speedlimit") || viewId.contains("max_speed")) {
                val num = extractNumber("$text $desc")
                if (num in 5..200) foundSpeedLimit = num
            }
            if (viewId.contains("current_speed") || viewId.contains("speedometer")) {
                val num = extractNumber("$text $desc")
                if (num in 0..300) foundCurrentSpeed = num
            }
        }

        updateAndBroadcast(foundCurrentSpeed, foundSpeedLimit, foundRoadName, "vietmap")
    }

    /**
     * Parse DatMap bubble: thường chỉ hiện số tốc độ giới hạn
     */
    private fun parseDatMapData(texts: List<NodeTextInfo>) {
        var foundSpeedLimit = 0

        for (info in texts) {
            val combined = info.contentDescription.ifBlank { info.text }.trim()
            if (combined.isBlank()) continue

            // DatMap bubble: số đơn thuần = tốc độ giới hạn
            val number = combined.replace(Regex("[^0-9]"), "")
            if (number.isNotEmpty()) {
                val value = number.toIntOrNull()
                if (value != null && value in 5..150) {
                    foundSpeedLimit = value
                }
            }

            // Hoặc format "Giới hạn XX km/h"
            val limitMatch = Regex("""(\d{2,3})\s*(?:km/?h)?""").find(combined)
            if (limitMatch != null) {
                val v = limitMatch.groupValues[1].toIntOrNull()
                if (v != null && v in 5..200) foundSpeedLimit = v
            }
        }

        if (foundSpeedLimit > 0) {
            updateAndBroadcast(-1, foundSpeedLimit, "", "datmap")
        }
    }

    /**
     * Parse Google Maps navigation nodes (dựa trên real device logcat):
     *
     * Key nodes:
     * - id/step_instruction_container: desc="1.7 kilometers, Keep left" hoặc "Head west"
     * - id/distance_text: text="1.7 km" (chỉ khi gần lượt rẽ)
     * - id/top_cue_text: text="CT37 Đ. Vành Đai 3" (road name)
     * - id/navigation_time_remaining_label: text="36 min"
     * - (no id): desc="Distance remaining is 12 km, estimated time of arrival is 10:20 AM"
     * - id/next_step_instruction_container: desc="Then Keep left" (next turn)
     */
    private var lastGoogleMapsSendTime = 0L

    private fun parseGoogleMapsData(texts: List<NodeTextInfo>) {
        // Throttle: gửi tối đa 1 lần/giây
        val now = System.currentTimeMillis()
        if (now - lastGoogleMapsSendTime < 1000) return

        var direction = ""
        var distance = ""
        var roadName = ""
        var timeRemaining = ""
        var eta = ""
        var totalDistance = ""
        var instruction = ""

        for (info in texts) {
            val viewId = info.viewId
            val text = info.text.trim()
            val desc = info.contentDescription.trim()

            when {
                // Step instruction: "1.7 kilometers, Keep left" hoặc "Head west"
                viewId.contains("step_instruction_container") && !viewId.contains("next_step") -> {
                    if (desc.isNotBlank()) {
                        instruction = desc
                        direction = parseGoogleDirection(desc)
                        // Thử parse distance từ instruction
                        val distMatch = Regex("""([\d.,]+)\s*(kilometers?|meters?|km|m|miles?|mi|feet|ft)""", RegexOption.IGNORE_CASE).find(desc)
                        if (distMatch != null) {
                            val value = distMatch.groupValues[1].replace(",", ".")
                            val unit = distMatch.groupValues[2].lowercase()
                            distance = when {
                                unit.startsWith("kilometer") || unit == "km" -> "$value km"
                                unit.startsWith("meter") || unit == "m" -> "$value m"
                                unit.startsWith("mile") || unit == "mi" -> "$value mi"
                                else -> "$value $unit"
                            }
                        }
                    }
                }

                // Distance to next maneuver (ưu tiên cao hơn)
                viewId.contains("distance_text") -> {
                    if (text.isNotBlank()) distance = text
                }

                // Road name for next turn
                viewId.contains("top_cue_text") -> {
                    if (text.isNotBlank()) roadName = text
                }

                // Time remaining
                viewId.contains("navigation_time_remaining") -> {
                    if (text.isNotBlank()) timeRemaining = text
                }

                // Bottom bar ETA (từ contentDescription)
                else -> {
                    if (desc.contains("estimated time of arrival", ignoreCase = true) ||
                        desc.contains("Time until arrival", ignoreCase = true)) {
                        val etaMatch = Regex("""(\d{1,2}:\d{2}\s*[AP]M)""", RegexOption.IGNORE_CASE).find(desc)
                        if (etaMatch != null) eta = etaMatch.groupValues[1]
                        val distRemMatch = Regex("""([\d.,]+)\s*(km|m|mi)""").find(desc)
                        if (distRemMatch != null) totalDistance = "${distRemMatch.groupValues[1]} ${distRemMatch.groupValues[2]}"
                    }
                    // Text field "12 km  •  10:20 AM"
                    if (text.contains("•") && text.contains(":") && viewId.isBlank()) {
                        val parts = text.split("•").map { it.trim() }
                        if (parts.size >= 2) {
                            totalDistance = parts[0].trim()
                            eta = parts[1].trim()
                        }
                    }
                }
            }
        }

        // Gửi nếu có data
        if (direction.isNotBlank() || distance.isNotBlank() || roadName.isNotBlank()) {
            lastGoogleMapsSendTime = now
            Log.i(TAG, "📍 [google_maps] dir=$direction dist=$distance road=$roadName time=$timeRemaining eta=$eta")

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
    }

    /**
     * Parse direction từ Google Maps instruction description.
     * Examples: "Keep left", "Turn right", "Go straight", "Make a U-turn",
     *           "Take the exit", "Merge", "Roundabout"
     */
    private fun parseGoogleDirection(desc: String): String {
        val lower = desc.lowercase()
        return when {
            lower.contains("u-turn") || lower.contains("u turn") -> "u_turn"
            lower.contains("sharp left") -> "sharp_left"
            lower.contains("sharp right") -> "sharp_right"
            lower.contains("keep left") || lower.contains("slight left") || lower.contains("bear left") -> "slight_left"
            lower.contains("keep right") || lower.contains("slight right") || lower.contains("bear right") -> "slight_right"
            lower.contains("turn left") || lower.contains("left") -> "turn_left"
            lower.contains("turn right") || lower.contains("right") -> "turn_right"
            lower.contains("straight") || lower.contains("continue") || lower.contains("head") -> "straight"
            lower.contains("merge") -> "merge"
            lower.contains("exit") -> "exit_right"
            lower.contains("roundabout") || lower.contains("rotary") -> "roundabout"
            lower.contains("arrive") || lower.contains("destination") -> "arrive"
            else -> "straight"
        }
    }

    /**
     * Update state và gửi data lên CarNavApplication → BLE → ESP32
     */
    private fun updateAndBroadcast(speed: Int, limit: Int, road: String, source: String) {
        val changed = (limit > 0 && limit != currentSpeedLimit) ||
                (speed >= 0 && speed != currentSpeed) ||
                (road.isNotBlank() && road != currentRoadName)

        if (limit > 0) currentSpeedLimit = limit
        if (speed >= 0) currentSpeed = speed
        if (road.isNotBlank()) currentRoadName = road
        lastUpdateTime = System.currentTimeMillis()

        if (changed) {
            Log.i(TAG, "📍 [$source] limit=${currentSpeedLimit}km/h speed=${currentSpeed}km/h road=$currentRoadName")

            val app = application as? CarNavApplication ?: return
            app.onAccessibilityUpdate(
                speedLimit = currentSpeedLimit,
                currentSpeed = currentSpeed,
                roadName = currentRoadName,
                source = source
            )
        }
    }

    private fun extractNumber(text: String): Int {
        val match = Regex("""(\d+)""").find(text)
        return match?.groupValues?.get(1)?.toIntOrNull() ?: 0
    }

    /**
     * Recursively collect all text from accessibility tree
     */
    private fun collectAllTexts(node: AccessibilityNodeInfo, texts: MutableList<NodeTextInfo>, depth: Int) {
        val text = node.text?.toString() ?: ""
        val contentDesc = node.contentDescription?.toString() ?: ""
        val viewId = node.viewIdResourceName ?: ""
        val className = node.className?.toString() ?: ""

        if (text.isNotBlank() || contentDesc.isNotBlank()) {
            texts.add(NodeTextInfo(text, contentDesc, viewId, className, depth))
        }

        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            try {
                collectAllTexts(child, texts, depth + 1)
            } finally {
                child.recycle()
            }
        }
    }

    private data class NodeTextInfo(
        val text: String,
        val contentDescription: String,
        val viewId: String,
        val className: String,
        val depth: Int
    )
}
