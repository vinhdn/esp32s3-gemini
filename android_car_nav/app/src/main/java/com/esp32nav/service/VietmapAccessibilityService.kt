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
                AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED or
                AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED or
                AccessibilityEvent.TYPE_ANNOUNCEMENT
        info.feedbackType = AccessibilityServiceInfo.FEEDBACK_GENERIC
        info.flags = AccessibilityServiceInfo.FLAG_REPORT_VIEW_IDS or
                AccessibilityServiceInfo.FLAG_INCLUDE_NOT_IMPORTANT_VIEWS or
                AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS or
                AccessibilityServiceInfo.FLAG_REQUEST_ENHANCED_WEB_ACCESSIBILITY
        info.notificationTimeout = 300
        // Monitor ALL packages to catch Vietmap even on secondary display
        // packageNames = null means all packages
        info.packageNames = null
        serviceInfo = info

        // Log active windows to debug multi-display
        try {
            val windows = windows
            Log.i(TAG, "🪟 Accessible windows: ${windows.size}")
            windows.forEach { window ->
                Log.i(TAG, "  Window: title='${window.title}' type=${window.type} layer=${window.layer}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Cannot enumerate windows: ${e.message}")
        }
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event == null) return
        val packageName = event.packageName?.toString() ?: return

        when (event.eventType) {
            AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED,
            AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED -> {
                // Nếu event từ package đã biết → xử lý trực tiếp
                val isMonitored = MONITORED_PACKAGES.any { packageName.contains(it) || it.contains(packageName) }
                if (isMonitored) {
                    isNavigating = true
                    processWindowContent(packageName)
                } else {
                    // Event từ package khác (có thể là launcher chứa Vietmap embed)
                    // → Scan tất cả windows tìm Vietmap content
                    scanAllWindows()
                }
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

    /**
     * Scan tất cả accessible windows — tìm Vietmap/Maps content ngay cả khi
     * app được embed trong launcher (ActivityView/TaskView trên AOSP Automotive).
     *
     * getWindows() trả về tất cả windows bao gồm cả embedded activities.
     */
    private var lastScanAllTime = 0L

    private fun scanAllWindows() {
        // Throttle: scan tối đa 1 lần/2 giây (tránh CPU hog)
        val now = System.currentTimeMillis()
        if (now - lastScanAllTime < 2000) return
        lastScanAllTime = now

        try {
            val allWindows = windows ?: return
            for (window in allWindows) {
                val root = window.root ?: continue
                try {
                    val allTexts = mutableListOf<NodeTextInfo>()
                    collectAllTexts(root, allTexts, 0)

                    if (allTexts.isEmpty()) continue

                    // Kiểm tra xem window này có chứa Vietmap content không
                    // Dấu hiệu: có node với "km/h" (vietmap speed display)
                    val hasVietmapContent = allTexts.any { info ->
                        val combined = "${info.text} ${info.contentDescription}"
                        combined.contains("km/h", ignoreCase = true) &&
                        combined.split("\n").size >= 2
                    }

                    // Dấu hiệu Google Maps: có node với view id chứa "maps"
                    val hasGoogleMapsContent = allTexts.any { info ->
                        info.viewId.contains("com.google.android.apps.maps")
                    }

                    when {
                        hasVietmapContent -> {
                            isNavigating = true
                            parseVietmapData(allTexts)
                            if (System.currentTimeMillis() - lastDebugLog > 10000) {
                                lastDebugLog = System.currentTimeMillis()
                                Log.d(TAG, "=== [embedded vietmap] ${allTexts.size} nodes (window: ${window.title}) ===")
                                allTexts.take(10).forEach { info ->
                                    Log.d(TAG, "  [${info.viewId}] text='${info.text}' desc='${info.contentDescription}'")
                                }
                            }
                        }
                        hasGoogleMapsContent -> {
                            isNavigating = true
                            parseGoogleMapsData(allTexts)
                        }
                    }
                } finally {
                    root.recycle()
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "scanAllWindows error: ${e.message}")
        }
    }

    private fun processWindowContent(packageName: String) {
        val rootNode = try {
            rootInActiveWindow
        } catch (e: Exception) {
            Log.w(TAG, "Cannot get rootInActiveWindow: ${e.message}")
            // Fallback: scan all windows
            scanAllWindows()
            return
        }

        if (rootNode == null) {
            // rootInActiveWindow null — thử scan all windows
            scanAllWindows()
            return
        }

        try {
            val allTexts = mutableListOf<NodeTextInfo>()
            collectAllTexts(rootNode, allTexts, 0)

            when {
                packageName.contains("vietmap") -> parseVietmapData(allTexts)
                packageName == "com.datmap.app" -> parseDatMapData(allTexts)
                packageName == "com.google.android.apps.maps" -> parseGoogleMapsData(allTexts)
            }

            // Debug log every 10 seconds
            if (System.currentTimeMillis() - lastDebugLog > 5000) {
                lastDebugLog = System.currentTimeMillis()
                Log.d(TAG, "=== [$packageName] ${allTexts.size} nodes ===")
                allTexts.forEach { info ->
                    if (info.text.isNotBlank() || info.contentDescription.isNotBlank()) {
                        Log.d(TAG, "  d=${info.depth} [${info.viewId}] cls=${info.className} text='${info.text}' desc='${info.contentDescription}'")
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
     *         hoặc text node riêng chứa số tốc độ giới hạn
     *
     * Trên một số head unit, Vietmap hiển thị speed limit dưới dạng:
     * - ImageView có contentDescription = "50" (số tốc độ giới hạn)
     * - TextView text = "50" gần biển báo
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
                continue
            }

            // Fallback: view ID based detection
            val viewId = info.viewId.lowercase()
            if (viewId.contains("speed_limit") || viewId.contains("speedlimit") || 
                viewId.contains("max_speed") || viewId.contains("limit")) {
                val num = extractNumber("$text $desc")
                if (num in 5..200) foundSpeedLimit = num
                continue
            }
            if (viewId.contains("current_speed") || viewId.contains("speedometer") ||
                viewId.contains("speed_value")) {
                val num = extractNumber("$text $desc")
                if (num in 0..300) foundCurrentSpeed = num
                continue
            }

            // Số đơn thuần trong biển báo tốc độ (ImageView/TextView không có viewId)
            // Thường nằm ở node nhỏ, className là ImageView hoặc FrameLayout
            if (combined.length <= 3 && combined.all { it.isDigit() }) {
                val num = combined.toIntOrNull()
                if (num != null && num in 20..150 && num % 10 == 0) {
                    // Số tròn chục (20,30,40,50,60,70,80,90,100,110,120) → speed limit
                    if (foundSpeedLimit == 0) foundSpeedLimit = num
                }
                continue
            }

            // Road name: doesn't contain km/h, has meaningful text
            if (combined.length > 3 && !combined.all { it.isDigit() }) {
                val lines = combined.split("\n").map { it.trim() }
                val candidate = lines.firstOrNull() ?: ""
                // Avoid picking up random UI elements
                if (candidate.length in 4..100 && !candidate.contains("km/h") &&
                    !candidate.matches(Regex("""^\d+.*"""))) {
                    foundRoadName = candidate
                }
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
     * Parse Google Maps navigation nodes.
     *
     * Trên head unit, Google Maps có thể không dùng view IDs chuẩn.
     * Strategy:
     * 1. Ưu tiên parse theo view ID nếu có
     * 2. Fallback: parse contentDescription chứa instruction/distance
     * 3. Detect "Distance remaining..." description
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

        val distRegex = Regex("""([\d.,]+)\s*(kilometers?|meters?|km|m|miles?|mi|feet|ft|mét|kilômét)""", RegexOption.IGNORE_CASE)
        val etaRegex = Regex("""(\d{1,2}:\d{2})\s*([AP]M|SA|CH)?""", RegexOption.IGNORE_CASE)

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
                        val distMatch = distRegex.find(desc)
                        if (distMatch != null) {
                            distance = formatDistance(distMatch.groupValues[1], distMatch.groupValues[2])
                        }
                    }
                }

                // Distance to next maneuver (ưu tiên cao hơn)
                viewId.contains("distance_text") -> {
                    if (text.isNotBlank()) distance = text
                }

                // Road name for next turn
                viewId.contains("top_cue_text") || viewId.contains("cue_text") -> {
                    if (text.isNotBlank()) roadName = text
                }

                // Time remaining
                viewId.contains("navigation_time_remaining") || viewId.contains("time_remaining") -> {
                    if (text.isNotBlank()) timeRemaining = text
                }

                // Fallback: parse by content when no known view IDs
                else -> {
                    // "Distance remaining is 12 km, estimated time of arrival is 10:20 AM"
                    if (desc.contains("estimated time of arrival", ignoreCase = true) ||
                        desc.contains("Time until arrival", ignoreCase = true) ||
                        desc.contains("Dự kiến", ignoreCase = true) ||
                        desc.contains("còn lại", ignoreCase = true)) {
                        val etaMatch = etaRegex.find(desc)
                        if (etaMatch != null) eta = "${etaMatch.groupValues[1]} ${etaMatch.groupValues[2]}".trim()
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

                    // Fallback: contentDescription chứa distance + direction
                    // e.g. "270 mét - về hướng Mỹ Đình" or "1.7 kilometers, Keep left"
                    if (desc.isNotBlank() && instruction.isBlank() && viewId.isBlank()) {
                        val distMatch = distRegex.find(desc)
                        if (distMatch != null) {
                            val possibleInstruction = desc.substring(distMatch.range.last + 1)
                                .trim().removePrefix(",").removePrefix("-").removePrefix("–").trim()
                            if (possibleInstruction.isNotBlank() && possibleInstruction.length > 2) {
                                distance = formatDistance(distMatch.groupValues[1], distMatch.groupValues[2])
                                instruction = possibleInstruction
                                direction = parseGoogleDirection(possibleInstruction)
                            }
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

    private fun formatDistance(value: String, unit: String): String {
        val cleanValue = value.replace(",", ".")
        return when (unit.lowercase()) {
            "kilometer", "kilometers", "kilômét" -> "$cleanValue km"
            "meter", "meters", "mét" -> "$cleanValue m"
            "km", "m", "mi", "ft" -> "$cleanValue $unit"
            "mile", "miles" -> "$cleanValue mi"
            "feet" -> "$cleanValue ft"
            else -> "$cleanValue $unit"
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
