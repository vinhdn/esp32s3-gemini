package com.esp32nav.service

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.AccessibilityServiceInfo
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import com.esp32nav.CarNavApplication
import com.esp32nav.carhost.VmsxFrame

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

        // Vị trí (tọa độ màn hình thật) của bong bóng nổi VietMap Live hiện
        // tại — null nếu chưa tìm thấy/đang không hiển thị. Dùng để crop
        // frame MediaProjection thay vì đọc text (bong bóng có 2 icon biển
        // báo dạng ảnh, không phải text, nên parse text không lấy đủ).
        @Volatile var bubbleBoundsInScreen: Rect? = null

        private const val BUBBLE_PACKAGE = "vn.vietmap.live"
        // Bong bóng là 1 window nhỏ (khác hẳn window app chính full-screen).
        // Giới hạn trên để loại bỏ nhầm với window MainActivity full-screen.
        private const val BUBBLE_MAX_AREA_FRACTION = 0.25f
    }

    private var lastDebugLog = 0L
    private val boundsHandler = Handler(Looper.getMainLooper())
    private var boundsPollStarted = false

    override fun onServiceConnected() {
        super.onServiceConnected()
        isServiceRunning = true
        Log.i(TAG, "✅ Accessibility Service connected - monitoring Vietmap/DatMap/Maps")
        startBubbleBoundsPolling()

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
        // 300ms trước đây làm hệ thống gộp (coalesce) sự kiện lâu hơn cần
        // thiết, cộng dồn vào độ trễ bắt thay đổi trên bong bóng.
        info.notificationTimeout = 100
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
            AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED,
            // TYPE_VIEW_TEXT_CHANGED: sự kiện Android bắn ra khi TextView.setText()
            // — bong bóng đổi số tốc độ/khoảng cách chủ yếu qua đường này, KHÔNG
            // phải TYPE_WINDOW_CONTENT_CHANGED. Trước đây có subscribe (info.eventTypes)
            // nhưng không xử lý ở đây → bỏ lỡ phần lớn thay đổi thực tế, đây là
            // nguyên nhân chính gây "chậm/hay miss" khi bắt trạng thái bong bóng.
            AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED -> {
                // QUAN TRỌNG: bong bóng nổi của VietMap là SYSTEM_ALERT_WINDOW
                // overlay, KHÔNG BAO GIỜ là "active window" (chỉ Activity toàn
                // màn hình mới active) — rootInActiveWindow (processWindowContent)
                // không bao giờ thấy nó. Luôn dùng scanAllWindows() (duyệt
                // getWindows(), thấy được cả overlay) làm đường chính; giữ
                // processWindowContent() cho trường hợp app full-screen thật.
                val isMonitored = MONITORED_PACKAGES.any { packageName.contains(it) || it.contains(packageName) }
                if (isMonitored) {
                    isNavigating = true
                }
                scanAllWindows()
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
        boundsPollStarted = false
        boundsHandler.removeCallbacksAndMessages(null)
        bubbleBoundsInScreen = null
        Log.i(TAG, "Accessibility Service destroyed")
    }

    /**
     * Bong bóng có thể bị kéo-thả bất kỳ lúc nào, không phải lúc nào cũng
     * sinh AccessibilityEvent (đặc biệt trong lúc drag) — nên poll định kỳ
     * thay vì chỉ dựa vào event, để bounds luôn theo kịp vị trí thật.
     */
    private fun startBubbleBoundsPolling() {
        if (boundsPollStarted) return
        boundsPollStarted = true
        val poll = object : Runnable {
            override fun run() {
                if (!boundsPollStarted) return
                updateBubbleBounds()
                boundsHandler.postDelayed(this, 500)
            }
        }
        boundsHandler.post(poll)
    }

    /**
     * Tìm window overlay (SYSTEM_ALERT_WINDOW) của vn.vietmap.live nhỏ nhất
     * đang hiển thị — đó chính là bong bóng nổi (window app chính full-screen
     * bị loại vì vượt BUBBLE_MAX_AREA_FRACTION). Lưu bounds vào
     * bubbleBoundsInScreen (tọa độ pixel màn hình thật) cho
     * MapStreamManager crop khi chụp qua MediaProjection.
     */
    private fun updateBubbleBounds() {
        try {
            val allWindows = windows ?: run {
                bubbleBoundsInScreen = null
                return
            }
            val screenArea = resources.displayMetrics.let { it.widthPixels.toLong() * it.heightPixels.toLong() }
            var best: Rect? = null
            var bestArea = Long.MAX_VALUE

            for (window in allWindows) {
                val root = window.root
                val pkg = root?.packageName?.toString()
                root?.recycle()
                if (pkg != BUBBLE_PACKAGE) continue

                val bounds = Rect()
                window.getBoundsInScreen(bounds)
                val area = bounds.width().toLong() * bounds.height().toLong()
                if (area <= 0 || area > screenArea * BUBBLE_MAX_AREA_FRACTION) continue

                if (area < bestArea) {
                    bestArea = area
                    best = Rect(bounds)
                }
            }

            if (best != bubbleBoundsInScreen) {
                Log.d(TAG, "Bubble bounds: $best")
            }
            bubbleBoundsInScreen = best
        } catch (e: Exception) {
            Log.e(TAG, "updateBubbleBounds error: ${e.message}")
        }
    }

    /**
     * Scan tất cả accessible windows — tìm Vietmap/Maps content ngay cả khi
     * app được embed trong launcher (ActivityView/TaskView trên AOSP Automotive).
     *
     * getWindows() trả về tất cả windows bao gồm cả embedded activities.
     */
    private var lastScanAllTime = 0L

    private fun scanAllWindows() {
        // Throttle: trước là 2000ms — quá chậm cho bong bóng đang lái, hay bị
        // miss thay đổi (biển báo/khoảng cách đổi liên tục). 600ms vẫn đủ để
        // tránh CPU hog vì mỗi scan chỉ vài window nhỏ.
        val now = System.currentTimeMillis()
        if (now - lastScanAllTime < 600) return
        lastScanAllTime = now

        try {
            val allWindows = windows ?: return
            for (window in allWindows) {
                val root = window.root ?: continue
                try {
                    // Bong bóng nổi VietMap Live: đọc trực tiếp theo view ID
                    // cố định (xác nhận qua dump thật) thay vì đoán theo
                    // pattern text như parseVietmapData cũ (dành cho định
                    // dạng bubble khác/cũ hơn, không khớp bản hiện tại).
                    if (root.packageName?.toString() == BUBBLE_PACKAGE) {
                        parseBubbleWidget(window, root)
                    }

                    // VietMap Live: CHỈ lấy dữ liệu từ bong bóng (parseBubbleWidget ở
                    // trên, theo view ID cố định) — đã bỏ heuristic "embedded vietmap"
                    // (đoán content theo pattern "km/h" trên toàn bộ node tree của mọi
                    // window) vì lỗi thời (dành cho định dạng bubble cũ) và tốn CPU
                    // (duyệt toàn bộ tree của MỌI window mỗi lần scan).
                    if (root.packageName?.toString() == BUBBLE_PACKAGE) continue

                    val allTexts = mutableListOf<NodeTextInfo>()
                    collectAllTexts(root, allTexts, 0)

                    if (allTexts.isEmpty()) continue

                    // Dấu hiệu Google Maps: có node với view id chứa "maps"
                    val hasGoogleMapsContent = allTexts.any { info ->
                        info.viewId.contains("com.google.android.apps.maps")
                    }

                    if (hasGoogleMapsContent) {
                        isNavigating = true
                        parseGoogleMapsData(allTexts)
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

    // ─── Bong bóng nổi VietMap Live: đọc trực tiếp theo view ID cố định ───
    // (xác nhận qua dump thật trên thiết bị) — chụp NGUYÊN bong bóng rồi gửi
    // thẳng qua board, thay UI toc do cũ trên board (đã xoá, xem
    // ui_screens.c/app_main.c) bằng đúng ảnh bong bóng. Không dùng
    // MediaProjection — chụp bằng AccessibilityService.takeScreenshotOfWindow()
    // (API 34+, chụp ĐÚNG window bong bóng, không cần crop) hoặc
    // takeScreenshot() toàn màn hình + crop theo bubbleBoundsInScreen (API
    // 30-33, thiết bị test hiện tại là Android 12 nên luôn đi nhánh này).
    // Không cần xin quyền riêng, chỉ cần capability canTakeScreenshot đã khai
    // báo trong accessibility_service_config.xml.

    private var lastVmsxCurrentSpeed = Int.MIN_VALUE
    private var lastVmsxSpeedLimit = Int.MIN_VALUE
    private var lastVmsxNextLimit = Int.MIN_VALUE
    private var lastVmsxNextLimitDistance = Int.MIN_VALUE
    private var lastVmsxCameraDistance = Int.MIN_VALUE

    /**
     * Bong bóng có 2 khu cảnh báo ĐỘC LẬP — xác nhận qua dump thật trên
     * thiết bị (kiểu bong bóng "sq_", cùng lúc trái=186m phải=365m):
     *   sq_upcoming_alert_left  -> biển báo tốc độ sắp tới (khoảng cách riêng)
     *   sq_upcoming_alert_right -> camera (khoảng cách riêng)
     * (kiểu bong bóng cũ hơn "h_" chỉ có 1 bên, id "warning_placeholder_id"/
     * "place_holder_textView" thay vì "warning_alert_image" — tương đương
     * bên trái, không có bên phải/camera).
     * Cả 2 bên dùng CHUNG id lá "warning_speed_distance_text_view" nên phải
     * theo dõi node cha (..._upcoming_alert_left/right) để phân biệt, không
     * thể chỉ so khớp theo id lá như trước (làm mất dữ liệu 1 bên — bên sau
     * duyệt cây sẽ ghi đè bên trước).
     */
    private fun parseBubbleWidget(window: AccessibilityWindowInfo, root: AccessibilityNodeInfo) {
        var currentSpeedText: String? = null
        var speedLimitText: String? = null
        var nextLimitText: String? = null
        var nextLimitDistanceText: String? = null
        var cameraDistanceText: String? = null

        // root.refresh() TRƯỚC khi duyệt: khi VietMap Live ở BACKGROUND (chỉ
        // còn bong bóng), AccessibilityNodeInfo trả về từ getWindows()/
        // window.root có thể là snapshot CŨ do hệ thống cache node info —
        // bong bóng trên màn hình đã đổi số thật nhưng cây node đọc được vẫn
        // giữ giá trị lúc trước. refresh() ép lấy lại state mới nhất từ
        // window nguồn. Đây là nguyên nhân "VMSX log vẫn dữ liệu cũ khi ở
        // background" — foreground thường xuyên có layout pass mới nên cache
        // tự invalidate, background thì không.
        try {
            root.refresh()
        } catch (_: Exception) {
        }

        // side: 0 = ngoài 2 khu cảnh báo, 1 = đang ở nhánh "..._left", 2 = "..._right"
        fun visit(node: AccessibilityNodeInfo, side: Int) {
            val id = node.viewIdResourceName
            var childSide = side
            if (id != null) {
                when {
                    id.endsWith("upcoming_alert_left") -> childSide = 1
                    id.endsWith("upcoming_alert_right") -> childSide = 2
                }
                when {
                    id.endsWith("current_speed_textview") -> {
                        try { node.refresh() } catch (_: Exception) {}
                        currentSpeedText = node.text?.toString()
                    }
                    id.endsWith("speed_limit_widget_text_view") -> {
                        try { node.refresh() } catch (_: Exception) {}
                        speedLimitText = node.text?.toString()
                    }
                    id.endsWith("place_holder_textView") -> {
                        // Kiểu bong bóng "h_" cũ: số biển báo sắp tới đọc
                        // được trực tiếp (kiểu "sq_" mới không có, chỉ có icon).
                        try { node.refresh() } catch (_: Exception) {}
                        nextLimitText = node.text?.toString()
                    }
                    id.endsWith("warning_speed_distance_text_view") -> {
                        try { node.refresh() } catch (_: Exception) {}
                        val text = node.text?.toString()
                        when (side) {
                            1 -> nextLimitDistanceText = text
                            2 -> cameraDistanceText = text
                            // Kiểu "h_" cũ không bọc trong left/right riêng -
                            // coi như thuộc bên trái (biển báo sắp tới).
                            else -> nextLimitDistanceText = text
                        }
                    }
                }
            }
            for (i in 0 until node.childCount) {
                val child = node.getChild(i) ?: continue
                try {
                    visit(child, childSide)
                } finally {
                    child.recycle()
                }
            }
        }
        visit(root, 0)

        // Không tìm thấy node nào → không phải window bong bóng (vd window
        // app chính full-screen), bỏ qua.
        if (currentSpeedText == null && speedLimitText == null &&
            nextLimitText == null && nextLimitDistanceText == null && cameraDistanceText == null
        ) {
            return
        }

        val parsedSpeed = currentSpeedText?.trim()?.toIntOrNull() ?: -1
        val speedLimit = speedLimitText?.trim()?.let { if (it == "!") 0 else it.toIntOrNull() } ?: 0
        val nextLimit = nextLimitText?.trim()?.let { if (it == "--") 0 else it.toIntOrNull() } ?: 0
        val nextLimitDistance = parseDistanceMeters(nextLimitDistanceText)
        val cameraDistance = parseDistanceMeters(cameraDistanceText)

        // currentSpeedLimit/currentSpeed vẫn cập nhật cho UI trong app (xem
        // MainScreen) — không còn dùng để gửi VMSX qua board nữa.
        if (speedLimit > 0) currentSpeedLimit = speedLimit
        if (parsedSpeed >= 0) currentSpeed = parsedSpeed
        lastUpdateTime = System.currentTimeMillis()
        isNavigating = true

        val changed = parsedSpeed != lastVmsxCurrentSpeed || speedLimit != lastVmsxSpeedLimit ||
            nextLimit != lastVmsxNextLimit || nextLimitDistance != lastVmsxNextLimitDistance ||
            cameraDistance != lastVmsxCameraDistance
        lastVmsxCurrentSpeed = parsedSpeed
        lastVmsxSpeedLimit = speedLimit
        lastVmsxNextLimit = nextLimit
        lastVmsxNextLimitDistance = nextLimitDistance
        lastVmsxCameraDistance = cameraDistance

        val heartbeatDue = System.currentTimeMillis() - lastScreenshotAt >= heartbeatIntervalMs
        if (changed || heartbeatDue) {
            lastScreenshotAt = System.currentTimeMillis()
            // TẠM COMMENT: gửi bitmap bong bóng — chất lượng hiển thị trên
            // board kém (ảnh nhỏ/nén JPEG), chuyển sang gửi SỐ LIỆU (VMSX)
            // như board đã hiển thị trước đây (xem app_main.c: on_car_data/
            // on_nav_data nối lại vào ui_screens.c). Bật lại bằng cách bỏ
            // comment dòng dưới (và có thể bỏ dòng sendVmsxData ở trên).
            // captureAndSendBubbleImage(window.id)
            sendVmsxData(parsedSpeed, speedLimit, nextLimit, nextLimitDistance, cameraDistance)
        }
    }

    /**
     * Gửi 4 giá trị đọc được từ bong bóng dưới dạng frame VMSX (đúng định
     * dạng VmslRelay.smali/waze_hud_ble.c đã dùng) — board dùng lại UI số
     * liệu sẵn có (ui_screens.c) để hiển thị, không cần giải mã JPEG.
     *  - speedLimit          : biển báo tốc độ hiện tại (currentSpeedLimit ở trên)
     *  - currentSpeed        : tốc độ hiện tại
     *  - nextLimit           : biển báo tốc độ SẮP TỚI (chỉ có ở kiểu bong bóng "h_" cũ)
     *  - nextLimitDistanceM  : khoảng cách tới biển báo sắp tới đó
     *  - cameraDistanceM     : khoảng cách tới camera (khu cảnh báo bên PHẢI, độc lập)
     */
    private fun sendVmsxData(
        currentSpeed: Int,
        speedLimit: Int,
        nextLimit: Int,
        nextLimitDistanceM: Int,
        cameraDistanceM: Int,
    ) {
        val app = application as? CarNavApplication ?: return
        val frame = VmsxFrame.build(
            speedLimit = speedLimit,
            currentSpeed = currentSpeed.coerceAtLeast(0),
            nextLimitDistanceMeters = nextLimitDistanceM,
            nextLimitSpeedLimit = nextLimit,
            cameraDistanceMeters = cameraDistanceM,
            overSpeed = speedLimit > 0 && currentSpeed > speedLimit,
            hudConnected = true,
        )
        Log.i(TAG, "📊 VMSX gửi: speed=$currentSpeed limit=$speedLimit nextLimit=$nextLimit " +
            "nextLimitDist=${nextLimitDistanceM}m cameraDist=${cameraDistanceM}m")
        app.imageRelay.sendRawFrame(frame)
    }

    private var lastScreenshotAt = 0L

    // Sàn cứng giữa 2 lần chụp/gửi kể cả khi liên tục đổi — né giới hạn tần
    // suất của takeScreenshot()/takeScreenshotOfWindow() phía hệ thống VÀ
    // tránh làm board (giải mã JPEG trên ESP32) không xử lý kịp.
    private val screenshotMinIntervalMs = 500L

    // Khi bong bóng KHÔNG đổi (theo 4 giá trị theo dõi ở trên), vẫn gửi định
    // kỳ 1 lần/giây — bắt các thay đổi thuần hình ảnh (icon/màu) không phản
    // ánh qua text, mà không gửi dồn dập như trước.
    private val heartbeatIntervalMs = 1000L

    private fun captureAndSendBubbleImage(windowId: Int) {
        val now = System.currentTimeMillis()
        if (now - lastScreenshotAt < screenshotMinIntervalMs) return
        lastScreenshotAt = now

        // API 34+: chụp ĐÚNG window bong bóng, hệ thống tự crop đúng bounds
        // — nhanh hơn, ít dữ liệu hơn takeScreenshot() toàn màn hình.
        if (android.os.Build.VERSION.SDK_INT >= 34) {
            try {
                takeScreenshotOfWindow(
                    windowId,
                    mainExecutor,
                    object : TakeScreenshotCallback {
                        override fun onSuccess(result: ScreenshotResult) {
                            handleScreenshotResult(result, cropRect = null)
                        }

                        override fun onFailure(errorCode: Int) {
                            Log.w(TAG, "takeScreenshotOfWindow thất bại ($errorCode), thử full-screen")
                            captureFullScreenFallback()
                        }
                    }
                )
                return
            } catch (e: Exception) {
                Log.e(TAG, "takeScreenshotOfWindow lỗi: ${e.message}")
            }
        }
        captureFullScreenFallback()
    }

    /**
     * Chụp toàn màn hình bằng takeScreenshot() (API 30+) rồi crop theo
     * bubbleBoundsInScreen — dùng khi không có takeScreenshotOfWindow()
     * (API < 34, vd thiết bị test hiện tại là Android 12) hoặc khi nó lỗi.
     */
    private fun captureFullScreenFallback() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.R) {
            Log.w(TAG, "takeScreenshot() cần Android 11+, bỏ qua trên thiết bị này")
            return
        }
        val bubbleRect = bubbleBoundsInScreen ?: return
        try {
            takeScreenshot(
                android.view.Display.DEFAULT_DISPLAY,
                mainExecutor,
                object : TakeScreenshotCallback {
                    override fun onSuccess(result: ScreenshotResult) {
                        handleScreenshotResult(result, cropRect = bubbleRect)
                    }

                    override fun onFailure(errorCode: Int) {
                        Log.w(TAG, "takeScreenshot thất bại: $errorCode")
                    }
                }
            )
        } catch (e: Exception) {
            Log.e(TAG, "takeScreenshot lỗi: ${e.message}")
        }
    }

    /**
     * cropRect null (đường takeScreenshotOfWindow) → dùng nguyên ảnh, hệ
     * thống đã crop đúng window rồi. cropRect khác null (đường full-screen
     * fallback) → tự crop theo bounds bong bóng.
     */
    private fun handleScreenshotResult(result: ScreenshotResult, cropRect: Rect?) {
        try {
            val hwBitmap = android.graphics.Bitmap.wrapHardwareBuffer(result.hardwareBuffer, result.colorSpace)
            val bitmap = hwBitmap?.copy(android.graphics.Bitmap.Config.ARGB_8888, false)
            result.hardwareBuffer.close()
            if (bitmap == null) return
            val target = if (cropRect != null) {
                val c = cropSafe(bitmap, cropRect)
                bitmap.recycle()
                c
            } else {
                bitmap
            }
            if (target != null) {
                // Board (canvas 240x240, xem img_stream.c) không cần ảnh to
                // hơn thế — thu nhỏ trước khi nén để chắc chắn dưới
                // IMG_MAX_JPEG_SIZE (20KB) của firmware. Giảm thêm ~40% (240 ->
                // 144) so với trước để board (tjpgd ROM decoder) giải mã nhanh
                // hơn — thời gian decode phụ thuộc chủ yếu vào số pixel, không
                // chỉ dung lượng JPEG.
                val scaled = scaleToFit(target, 144)
                sendComposite(scaled)
                scaled.recycle()
            }
        } catch (e: Exception) {
            Log.e(TAG, "xử lý screenshot lỗi: ${e.message}")
        }
    }

    private fun cropSafe(source: android.graphics.Bitmap, rect: Rect?): android.graphics.Bitmap? {
        if (rect == null || rect.isEmpty) return null
        val l = rect.left.coerceIn(0, source.width - 1)
        val t = rect.top.coerceIn(0, source.height - 1)
        val r = rect.right.coerceIn(l + 1, source.width)
        val b = rect.bottom.coerceIn(t + 1, source.height)
        if (r <= l || b <= t) return null
        return try {
            android.graphics.Bitmap.createBitmap(source, l, t, r - l, b - t)
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Thu nhỏ (giữ tỷ lệ) nếu cạnh dài nhất > maxDim — không phóng to.
     */
    private fun scaleToFit(bitmap: android.graphics.Bitmap, maxDim: Int): android.graphics.Bitmap {
        val w = bitmap.width
        val h = bitmap.height
        if (w <= maxDim && h <= maxDim) return bitmap
        val scale = maxDim.toFloat() / maxOf(w, h)
        val newW = (w * scale).toInt().coerceAtLeast(1)
        val newH = (h * scale).toInt().coerceAtLeast(1)
        val scaled = android.graphics.Bitmap.createScaledBitmap(bitmap, newW, newH, true)
        bitmap.recycle()
        return scaled
    }

    // Để margin dưới IMG_MAX_JPEG_SIZE (20*1024 byte, xem img_stream.c) —
    // vượt ngưỡng đó firmware sẽ hủy frame (buffer overflow trong lúc ghép chunk).
    private val maxJpegBytes = 18 * 1024

    private fun sendComposite(bitmap: android.graphics.Bitmap) {
        val app = application as? CarNavApplication ?: return
        var quality = 85
        var bytes: ByteArray
        while (true) {
            val stream = java.io.ByteArrayOutputStream()
            bitmap.compress(android.graphics.Bitmap.CompressFormat.JPEG, quality, stream)
            bytes = stream.toByteArray()
            if (bytes.size <= maxJpegBytes || quality <= 25) break
            quality -= 20
        }
        if (bytes.size > maxJpegBytes) {
            Log.w(TAG, "bubble image vẫn ${bytes.size} byte (> $maxJpegBytes) sau khi giảm chất lượng, bỏ qua frame")
            return
        }
        Log.i(TAG, "🖼️ bubble image: gửi ${bytes.size} byte (q=$quality, ${bitmap.width}x${bitmap.height}) " +
            "tới board (connected=${app.imageRelay.isConnected})")
        app.imageRelay.sendJpegFrame(bytes)
    }

    /**
     * "--" / rỗng → 0 (không có cảnh báo). Có thể kèm đơn vị "m"/"km".
     */
    private fun parseDistanceMeters(raw: String?): Int {
        val text = raw?.trim() ?: return 0
        if (text.isBlank() || text == "--") return 0
        val match = Regex("""([\d.,]+)\s*(km|m)?""", RegexOption.IGNORE_CASE).find(text) ?: return 0
        val value = match.groupValues[1].replace(",", ".").toFloatOrNull() ?: return 0
        val unit = match.groupValues[2].lowercase()
        return if (unit == "km") (value * 1000).toInt() else value.toInt()
    }

    private data class NodeTextInfo(
        val text: String,
        val contentDescription: String,
        val viewId: String,
        val className: String,
        val depth: Int
    )
}
