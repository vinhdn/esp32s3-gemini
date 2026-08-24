package com.esp32nav.parser

import com.esp32nav.model.NavigationData

object GoogleMapsParser {

    private val directionPatterns = listOf(
        // English patterns
        DirectionPattern("u_turn", listOf("make a u-turn", "u-turn")),
        DirectionPattern("sharp_left", listOf("sharp left")),
        DirectionPattern("sharp_right", listOf("sharp right")),
        DirectionPattern("slight_left", listOf("slight left", "bear left", "keep left")),
        DirectionPattern("slight_right", listOf("slight right", "bear right", "keep right")),
        DirectionPattern("turn_left", listOf("turn left")),
        DirectionPattern("turn_right", listOf("turn right")),
        DirectionPattern("straight", listOf("head ", "continue ", "go straight", "straight")),
        DirectionPattern("arrive", listOf("arrive", "destination", "you have arrived")),
        DirectionPattern("roundabout", listOf("roundabout", "rotary", "traffic circle")),
        DirectionPattern("merge", listOf("merge")),
        DirectionPattern("fork_left", listOf("fork left", "take the left fork")),
        DirectionPattern("fork_right", listOf("fork right", "take the right fork")),
        DirectionPattern("exit_left", listOf("exit left")),
        DirectionPattern("exit_right", listOf("exit right", "take the exit")),

        // Vietnamese patterns
        DirectionPattern("u_turn", listOf("quay đầu")),
        DirectionPattern("sharp_left", listOf("rẽ gắt trái", "rẽ mạnh trái")),
        DirectionPattern("sharp_right", listOf("rẽ gắt phải", "rẽ mạnh phải")),
        DirectionPattern("slight_left", listOf("hơi rẽ trái", "chếch trái", "lệch trái", "đi về phía trái")),
        DirectionPattern("slight_right", listOf("hơi rẽ phải", "chếch phải", "lệch phải", "đi về phía phải")),
        DirectionPattern("turn_left", listOf("rẽ trái", "quẹo trái", "re trái")),
        DirectionPattern("turn_right", listOf("rẽ phải", "quẹo phải", "re phải")),
        DirectionPattern("straight", listOf("đi thẳng", "tiếp tục", "đi tiếp", "chạy thẳng")),
        DirectionPattern("arrive", listOf("đến nơi", "điểm đến", "đã đến")),
        DirectionPattern("roundabout", listOf("vòng xoay", "bùng binh", "vòng xuyến")),
        DirectionPattern("merge", listOf("nhập vào", "nhập làn")),
    )

    private val distanceRegex = Regex(
        """(\d+[.,]?\d*)\s*(km|m|mi|ft|metres|meters|kilom[eé]t(?:re)?s?|mét)""",
        RegexOption.IGNORE_CASE
    )

    private val etaRegex = Regex(
        """(\d{1,2}[:.]\d{2})\s*(AM|PM|SA|CH)?""",
        RegexOption.IGNORE_CASE
    )

    private val roadNamePatterns = listOf(
        // English: "onto X", "on X", "toward X", "via X"
        Regex("""(?:onto|on|toward|towards|via)\s+(.+)""", RegexOption.IGNORE_CASE),
        // Vietnamese: "vào X", "sang X", "theo X"
        Regex("""(?:vào|sang|theo|trên|ra)\s+(.+)""", RegexOption.IGNORE_CASE),
    )

    fun parse(title: String?, text: String?, subText: String?): NavigationData? {
        if (title.isNullOrBlank() && text.isNullOrBlank()) return null

        val instruction = title?.trim() ?: ""
        val fullText = listOfNotNull(title, text).joinToString(" ").lowercase()

        val direction = parseDirection(fullText)
        val distance = parseDistance(text ?: title ?: "")
        val road = parseRoad(instruction)
        val eta = parseEta(subText ?: text ?: "")

        val navData = NavigationData(
            direction = direction,
            distance = distance,
            road = road,
            eta = eta,
            instruction = instruction
        )

        return if (navData.isValid()) navData else null
    }

    private fun parseDirection(text: String): String {
        val lower = text.lowercase()
        for (pattern in directionPatterns) {
            for (keyword in pattern.keywords) {
                if (lower.contains(keyword)) {
                    return pattern.direction
                }
            }
        }
        // Fallback: look for generic left/right
        return when {
            lower.contains("left") || lower.contains("trái") -> "turn_left"
            lower.contains("right") || lower.contains("phải") -> "turn_right"
            else -> "straight"
        }
    }

    private fun parseDistance(text: String): String {
        val match = distanceRegex.find(text) ?: return ""
        val value = match.groupValues[1].replace(",", ".")
        val unit = when (match.groupValues[2].lowercase()) {
            "km", "kilometre", "kilometres", "kilométre", "kilométres", "kilômét" -> "km"
            "m", "metres", "meters", "mét" -> "m"
            "mi" -> "mi"
            "ft" -> "ft"
            else -> match.groupValues[2]
        }
        return "$value$unit"
    }

    private fun parseRoad(instruction: String): String {
        for (pattern in roadNamePatterns) {
            val match = pattern.find(instruction)
            if (match != null) {
                return match.groupValues[1].trim().trimEnd('.', ',', ';')
            }
        }
        return ""
    }

    private fun parseEta(text: String): String {
        val match = etaRegex.find(text) ?: return ""
        val time = match.groupValues[1].replace(".", ":")
        val amPm = match.groupValues[2]
        return if (amPm.isNotBlank()) "$time $amPm" else time
    }

    private data class DirectionPattern(
        val direction: String,
        val keywords: List<String>
    )
}
