package com.esp32nav.ble

import com.esp32nav.model.NavigationData
import com.esp32nav.model.VehicleData
import com.google.gson.JsonObject
import com.google.gson.JsonParser

object HlpProtocol {

    fun createHiMessage(): String = "{\"v\":1,\"t\":\"hi\"}\n"

    fun createByeMessage(): String = "{\"v\":1,\"t\":\"bye\"}\n"

    fun createPongMessage(): String = "{\"v\":1,\"t\":\"pong\"}\n"

    fun createNavMessage(data: NavigationData): String = data.toHlpJson()

    fun createVehicleMessage(data: VehicleData): String = data.toHlpJson()

    fun parseMessage(raw: String): HlpMessage? {
        return try {
            val json = JsonParser.parseString(raw.trim()).asJsonObject
            val version = json.get("v")?.asInt ?: 1
            val type = json.get("t")?.asString ?: return null
            HlpMessage(version, type, json)
        } catch (e: Exception) {
            null
        }
    }

    fun chunkData(data: ByteArray, mtu: Int): List<ByteArray> {
        val chunkSize = maxOf(mtu - 3, 20)
        val chunks = mutableListOf<ByteArray>()
        var offset = 0
        while (offset < data.size) {
            val end = minOf(offset + chunkSize, data.size)
            chunks.add(data.copyOfRange(offset, end))
            offset = end
        }
        return chunks
    }
}

data class HlpMessage(
    val version: Int,
    val type: String,
    val payload: JsonObject
)
