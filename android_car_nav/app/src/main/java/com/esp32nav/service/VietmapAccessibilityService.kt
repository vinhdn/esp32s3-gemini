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
            "vn.vietmap.live.v2",
            "vn.vietmap.vietmaplive",
            "vn.vietmap.vietmap_map",
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
    private var lastProactiveScan = 0L
    private val PROACTIVE_SCAN_INTERVAL = 2000L  // Scan every 2 seconds

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

        // Proactive scan: even if event is from a different package, periodically scan
        // all windows for Vietmap data (handles secondary display where events might not fire)
        val now = System.currentTimeMillis()
        if (now - lastProactiveScan > PROACTIVE_SCAN_INTERVAL) {
            lastProactiveScan = now
            proactiveScanAllWindows()
        }

        // Filter only monitored packages
        val isMonitored = MONITORED_PACKAGES.any { packageName.contains(it) || it.contains(packageName) }
        if (!isMonitored) return

        isNavigating = true

        // Debug: log event source info periodically
        if (now - lastDebugLog > 10000) {
            Log.d(TAG, "📨 Event: type=${event.eventType} pkg=$packageName class=${event.className} " +
                    "windowId=${event.windowId} source=${event.source?.packageName}")
        }

        when (event.eventType) {
            AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED,
            AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED,
            AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED,
            AccessibilityEvent.TYPE_VIEW_SCROLLED -> {
                processWindowContent(packageName)
            }
        }
    }

    /**
     * Proactive scan: quét tất cả windows tìm Vietmap/DatMap/Maps
     * ngay cả khi event đến từ package khác (SystemUI, Launcher, etc.)
     *
     * Đây là giải pháp cho trường hợp Vietmap ở secondary display:
     * accessibility event vẫn fire từ primary display nhưng không trigger parse
     * cho Vietmap vì package name khác.
     */
    private fun proactiveScanAllWindows() {
        try {
            val allWindows = windows ?: return
            for (window in allWindows) {
                try {
                    val root = window.root ?: continue
                    val windowPkg = root.packageName?.toString() ?: ""

                    val matchedMonitor = MONITORED_PACKAGES.firstOrNull {
                        windowPkg.contains(it) || it.contains(windowPkg)
                    }

                    if (matchedMonitor != null) {
                        val allTexts = mutableListOf<NodeTextInfo>()
                        try {
                            collectAllTexts(root, allTexts, 0)
                            if (allTexts.isNotEmpty()) {
                                isNavigating = true
                                when {
                                    windowPkg.contains("vietmap") -> parseVietmapData(allTexts)
                                    windowPkg.contains("datmap") -> parseDatMapData(allTexts)
                                    windowPkg.contains("google.android.apps.maps") -> parseGoogleMapsData(allTexts)
                                }
                            }
                        } finally {
                            root.recycle()
                        }
                    } else {
                        root.recycle()
                    }
                } catch (e: Exception) {
                    // Skip problematic windows (widgets, system overlays, etc.)
                    continue
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Proactive scan failed: ${e.message}")
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
        // Strategy: iterate ALL accessible windows to find ones matching the package.
        // This works even when the target app is on a secondary display or not the active window.
        val rootNodes = mutableListOf<AccessibilityNodeInfo>()

        try {
            val allWindows = windows
            if (allWindows.isNullOrEmpty()) {
                // Fallback to rootInActiveWindow if windows API unavailable
                val fallback = rootInActiveWindow
                if (fallback != null) rootNodes.add(fallback)
            } else {
                for (window in allWindows) {
                    val root = try { window.root } catch (e: Exception) { null }
                    if (root == null) continue
                    // Check if this window belongs to the target package
                    // by inspecting root node's package name
                    val windowPkg = root.packageName?.toString() ?: ""
                    val matchesPackage = MONITORED_PACKAGES.any {
                        windowPkg.contains(it) || it.contains(windowPkg)
                    }
                    // Also accept if event packageName matches
                    val matchesEvent = windowPkg.contains(packageName) || packageName.contains(windowPkg)

                    if (matchesPackage || matchesEvent) {
                        rootNodes.add(root)
                    } else {
                        root.recycle()
                    }
                }

                // If no matching window found via windows API, try rootInActiveWindow
                if (rootNodes.isEmpty()) {
                    val fallback = try { rootInActiveWindow } catch (e: Exception) { null }
                    if (fallback != null) rootNodes.add(fallback)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Cannot enumerate windows, falling back to rootInActiveWindow: ${e.message}")
            val fallback = try { rootInActiveWindow } catch (ex: Exception) { null }
            if (fallback != null) rootNodes.add(fallback)
        }

        if (rootNodes.isEmpty()) {
            // Debug: log when we can't find any window
            if (System.currentTimeMillis() - lastDebugLog > 5000) {
                lastDebugLog = System.currentTimeMillis()
                Log.w(TAG, "⚠️ No accessible window found for package: $packageName")
                logAllWindows()
            }
            return
        }

        try {
            val allTexts = mutableListOf<NodeTextInfo>()
            for (root in rootNodes) {
                collectAllTexts(root, allTexts, 0)
            }

            when {
                packageName.contains("vietmap") -> parseVietmapData(allTexts)
                packageName == "com.datmap.app" -> parseDatMapData(allTexts)
                packageName == "com.google.android.apps.maps" -> parseGoogleMapsData(allTexts)
            }

            // Debug log every 5 seconds
            if (System.currentTimeMillis() - lastDebugLog > 5000) {
                lastDebugLog = System.currentTimeMillis()
                Log.d(TAG, "=== [$packageName] ${allTexts.size} nodes from ${rootNodes.size} window(s) ===")
                allTexts.forEach { info ->
                    if (info.text.isNotBlank() || info.contentDescription.isNotBlank()) {
                        Log.d(TAG, "  d=${info.depth} [${info.viewId}] cls=${info.className} text='${info.text}' desc='${info.contentDescription}'")
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error processing window: ${e.message}")
        } finally {
            rootNodes.forEach { it.recycle() }
        }
    }

    /**
     * Log tất cả windows đang accessible — dùng để debug multi-display.
     */
    private fun logAllWindows() {
        try {
            val allWindows = windows
            Log.i(TAG, "🪟 Total accessible windows: ${allWindows?.size ?: 0}")
            allWindows?.forEachIndexed { idx, window ->
                val root = window.root
                val pkg = root?.packageName?.toString() ?: "(no root)"
                val title = window.title?.toString() ?: "(no title)"
                Log.i(TAG, "  [$idx] pkg=$pkg title='$title' type=${window.type} layer=${window.layer} display=${window.displayId}")
                root?.recycle()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Cannot log windows: ${e.message}")
        }
    }

    /**
     * Parse Vietmap Live node tree.
     *
     * Trên head unit Baic (ActivityView/Virtual Display), node tree có view IDs:
     * - vn.vietmap.live:id/current_speed_textview → tốc độ hiện tại (text = "0", "45", etc.)
     * - vn.vietmap.live:id/unit_textView → "km/h"
     * - vn.vietmap.live:id/speed_limit_widget_text_view → tốc độ giới hạn ("50", "60", etc.)
     * - vn.vietmap.live:id/warning_speed_distance_text_view → khoảng cách camera ("43m")
     * - vn.vietmap.live:id/textView2 → "VIETMAP LIVE" (app name, bỏ qua)
     *
     * Fallback cho các head unit khác:
     * - contentDescription = "{speed}\nkm/h\n{limit}"
     * - contentDescription = "{road_name}\n{district}, {city}"
     */
    private fun parseVietmapData(texts: List<NodeTextInfo>) {
        var foundSpeedLimit = 0
        var foundCurrentSpeed = -1
        var foundRoadName = ""
        var usedViewIdParsing = false

        // === Phase 1: Parse by view ID (ưu tiên cao nhất) ===
        for (info in texts) {
            val viewId = info.viewId.lowercase()
            val text = info.text.trim()

            when {
                viewId.contains("current_speed_textview") || viewId.contains("current_speed") ||
                viewId.contains("speedometer") || viewId.contains("speed_value") -> {
                    val num = text.replace(Regex("[^0-9]"), "").toIntOrNull()
                    if (num != null && num in 0..300) {
                        foundCurrentSpeed = num
                        usedViewIdParsing = true
                    }
                }
                viewId.contains("speed_limit_widget") || viewId.contains("speed_limit") ||
                viewId.contains("speedlimit") || viewId.contains("max_speed") ||
                viewId.contains("limit") -> {
                    val num = text.replace(Regex("[^0-9]"), "").toIntOrNull()
                    if (num != null && num in 5..200) {
                        foundSpeedLimit = num
                        usedViewIdParsing = true
                    }
                }
                viewId.contains("road_name") || viewId.contains("street_name") ||
                viewId.contains("route_name") -> {
                    if (text.isNotBlank() && text.length in 2..100) {
                        foundRoadName = text
                        usedViewIdParsing = true
                    }
                }
            }
        }

        // === Phase 2: Fallback content-based parsing (nếu Phase 1 không tìm được) ===
        if (!usedViewIdParsing) {
            for (info in texts) {
                val desc = info.contentDescription.trim()
                val text = info.text.trim()
                val combined = desc.ifBlank { text }
                val viewId = info.viewId.lowercase()

                if (combined.isBlank()) continue

                // Skip known non-data view IDs
                if (viewId.contains("textview2") || viewId.contains("unit_textview") ||
                    viewId.contains("warning_speed_distance")) continue

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

                // Số đơn thuần trong biển báo tốc độ
                if (combined.length <= 3 && combined.all { it.isDigit() }) {
                    val num = combined.toIntOrNull()
                    if (num != null && num in 20..150 && num % 10 == 0) {
                        if (foundSpeedLimit == 0) foundSpeedLimit = num
                    }
                    continue
                }

                // Road name: doesn't contain km/h, has meaningful text
                if (combined.length > 3 && !combined.all { it.isDigit() } && viewId.isBlank()) {
                    val lines = combined.split("\n").map { it.trim() }
                    val candidate = lines.firstOrNull() ?: ""
                    if (candidate.length in 4..100 && !candidate.contains("km/h") &&
                        !candidate.contains("VIETMAP", ignoreCase = true) &&
                        !candidate.contains("Phiên bản", ignoreCase = true) &&
                        !candidate.matches(Regex("""^\d+.*"""))) {
                        foundRoadName = candidate
                    }
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
