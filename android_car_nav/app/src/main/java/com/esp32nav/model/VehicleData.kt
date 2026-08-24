package com.esp32nav.model

import com.google.gson.JsonObject

data class VehicleData(
    val speedKmh: Int = -1,         // OBD PID 0x0D
    val coolantTempC: Int = -999,   // OBD PID 0x05
    val intakeTempC: Int = -999,    // OBD PID 0x0F
    val oilTempC: Int = -999,       // OBD PID 0x5C
    val rpm: Int = -1,              // OBD PID 0x0C
    val tireFLkPa: Int = -1,       // Front Left
    val tireFRkPa: Int = -1,       // Front Right
    val tireRLkPa: Int = -1,       // Rear Left
    val tireRRkPa: Int = -1        // Rear Right
) {
    fun toHlpJson(): String {
        val json = JsonObject()
        json.addProperty("v", 1)
        json.addProperty("t", "veh")
        if (speedKmh >= 0) json.addProperty("spd", speedKmh)
        if (coolantTempC > -999) json.addProperty("coolant", coolantTempC)
        if (intakeTempC > -999) json.addProperty("intake", intakeTempC)
        if (oilTempC > -999) json.addProperty("oil", oilTempC)
        if (rpm >= 0) json.addProperty("rpm", rpm)

        val hasTires = tireFLkPa >= 0 || tireFRkPa >= 0 || tireRLkPa >= 0 || tireRRkPa >= 0
        if (hasTires) {
            val tires = JsonObject()
            if (tireFLkPa >= 0) tires.addProperty("fl", tireFLkPa)
            if (tireFRkPa >= 0) tires.addProperty("fr", tireFRkPa)
            if (tireRLkPa >= 0) tires.addProperty("rl", tireRLkPa)
            if (tireRRkPa >= 0) tires.addProperty("rr", tireRRkPa)
            json.add("tires", tires)
        }
        return json.toString() + "\n"
    }

    fun hasData(): Boolean = speedKmh >= 0 || rpm >= 0 || coolantTempC > -999
}
