package com.esp32nav.service

import android.app.Notification
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.drawable.BitmapDrawable
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.esp32nav.CarNavApplication
import com.esp32nav.parser.DatMapParser
import com.esp32nav.parser.GoogleMapsParser
import java.io.File
import java.io.FileOutputStream

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

        // Google Maps hien huong re THAT qua android.largeIcon (bitmap ve san),
        // KHONG PHAI qua text - doan qua text (parseDirectionFromInstruction)
        // co the sai (da xac nhan thuc te: dang "Make a U-turn" bi doan thanh
        // "straight"). Uu tien ket qua tra bang hash bitmap (chinh xac hon,
        // xay tu mau anh that thu thap luc lai xe that - xem
        // ICON_HASH_TO_DIRECTION), text-parsing chi con la fallback cho
        // huong con thieu mau (chech trai/phai, vong xuyen, nhap lan...).
        try {
            val largeIcon = notification.getLargeIcon()
            val drawable = largeIcon?.loadDrawable(this)
            val bmp = (drawable as? BitmapDrawable)?.bitmap
            if (bmp != null) {
                val hash = iconDHash(bmp)
                val matched = matchIconDirection(hash)
                Log.i(TAG, "🖼️ largeIcon dHash=0x%016X size=%dx%d doan-text=%s tra-bang=%s".format(
                    hash, bmp.width, bmp.height, direction, matched ?: "(khong khop)"))
                if (matched != null) direction = matched
                saveIconSample(bmp, hash) // van luu mau moi/chua biet de bo sung bang sau
            }
        } catch (e: Exception) {
            Log.w(TAG, "Khong doc duoc largeIcon: ${e.message}")
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

    /**
     * Bang tra "dHash bitmap icon That -> huong re" - xay tu mau anh THAT thu
     * thap luc lai xe that (icon Google Maps ve, khong phai doan qua text).
     * Moi lan them huong moi: xem file PNG luu trong
     * getExternalFilesDir(null)/nav_icons/ (pull qua `adb pull`, khong can
     * root) de xac nhan hinh dang bang mat roi them dong moi vao day.
     */
    private val ICON_HASH_TO_DIRECTION = mapOf(
        0x0010101054201000UL to "straight",   // mui ten thang + gach dut
        0x0080808880040800UL to "turn_left",  // mui ten cong trai
        0x0002024280c02000UL to "turn_right", // mui ten cong phai
        0x0080889288884000UL to "u_turn",     // mui ten quay dau
    )

    /** So bit khac nhau toi da van coi la "khop" - chong lech nhe do resize/nen. */
    private val ICON_HASH_MAX_DISTANCE = 6

    private fun matchIconDirection(hash: Long): String? {
        val h = hash.toULong()
        var best: String? = null
        var bestDist = Int.MAX_VALUE
        for ((refHash, dir) in ICON_HASH_TO_DIRECTION) {
            val dist = java.lang.Long.bitCount((h xor refHash).toLong())
            if (dist < bestDist) {
                bestDist = dist
                best = dir
            }
        }
        return if (bestDist <= ICON_HASH_MAX_DISTANCE) best else null
    }

    /**
     * "Dau van tay" 8x8 cua bitmap icon dan duong, dua tren kenh ALPHA (icon
     * la hinh mui ten trang tren nen trong suot, khong phai mau sac - dung
     * alpha thay vi luminance de khong bi anh huong boi mau tint cua app/theme).
     * Thuat toan dHash chuan: so sanh do sang 2 pixel ke nhau theo hang ngang,
     * ben trai sang hon -> bit 1. On dinh voi anh cung noi dung du bi resize/
     * nen nhe (khac vai bit thi van coi la giong qua Hamming distance).
     */
    private fun iconDHash(bmp: Bitmap): Long {
        val resized = Bitmap.createScaledBitmap(bmp, 9, 8, true)
        var hash = 0L
        var bit = 0
        for (y in 0 until 8) {
            for (x in 0 until 8) {
                val left = Color.alpha(resized.getPixel(x, y))
                val right = Color.alpha(resized.getPixel(x + 1, y))
                if (left > right) hash = hash or (1L shl bit)
                bit++
            }
        }
        return hash
    }

    /**
     * Luu anh PNG that vao bo nho ngoai rieng cua app (khong can quyen dac
     * biet, doc duoc qua `adb pull` khong can root) de doi chieu bang mat voi
     * huong re THAT quan sat duoc khi lai xe - ten file kem hash de de tra cuu
     * lai. Chi giu 1 ban/hash (ghi de) de khong day bo nho theo thoi gian.
     */
    private fun saveIconSample(bmp: Bitmap, hash: Long) {
        try {
            val dir = File(getExternalFilesDir(null), "nav_icons")
            if (!dir.exists()) dir.mkdirs()
            val file = File(dir, "icon_%016x.png".format(hash))
            if (file.exists()) return // da co mau nay roi
            FileOutputStream(file).use { out ->
                bmp.compress(Bitmap.CompressFormat.PNG, 100, out)
            }
            Log.i(TAG, "  -> da luu mau icon: ${file.absolutePath}")
        } catch (e: Exception) {
            Log.w(TAG, "Khong luu duoc mau icon: ${e.message}")
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
