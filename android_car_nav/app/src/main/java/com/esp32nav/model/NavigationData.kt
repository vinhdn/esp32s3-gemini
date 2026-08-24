package com.esp32nav.model

import com.google.gson.JsonObject

data class NavigationData(
    val direction: String = "",
    val distance: String = "",
    val road: String = "",
    val eta: String = "",
    val instruction: String = ""
) {
    fun isValid(): Boolean = direction.isNotBlank() || instruction.isNotBlank()

    fun toHlpJson(): String {
        val json = JsonObject()
        json.addProperty("v", 1)
        json.addProperty("t", "nav")
        if (direction.isNotBlank()) json.addProperty("dir", direction)
        if (distance.isNotBlank()) json.addProperty("dist", distance)
        if (road.isNotBlank()) json.addProperty("road", road)
        if (eta.isNotBlank()) json.addProperty("eta", eta)
        if (instruction.isNotBlank()) json.addProperty("instruction", instruction)
        return json.toString() + "\n"
    }
}
