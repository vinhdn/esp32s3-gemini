package com.esp32nav.carhost

/**
 * Dựng frame VMSX y hệt định dạng mà waze_hud_ble.c parse để gửi qua BLE cho
 * ESP32 board.
 *
 * v3 (20 byte) — thêm thời tiết (Open-Meteo, xem WeatherManager.kt), hiển
 * thị thay vòng tròn biển báo khi không có số: thời tiết HÔM NAY ở vị trí
 * vòng tròn tốc độ giới hạn hiện tại, thời tiết NGÀY MAI ở vị trí vòng tròn
 * biển báo sắp tới. v2 (16 byte, không có thời tiết) không còn được chấp
 * nhận — 2 phía luôn deploy cùng lúc, không cần tương thích ngược.
 *
 * offset  size  nội dung
 * 0       4     ASCII "VMSX"
 * 4       1     version = 3
 * 5       1     speedLimit (biển báo hiện tại)
 * 6       1     currentSpeed
 * 7       1     flags: bit0 overSpeed, bit1 underMinSpeedLimit,
 *                      bit2 hudConnected, bit3 nextLimit hợp lệ,
 *                      bit4 camera hợp lệ, bit5 thời tiết hôm nay hợp lệ,
 *                      bit6 thời tiết ngày mai hợp lệ
 * 8       1     minSpeedLimit
 * 9       1     navigationState
 * 10-11   2     khoảng cách tới biển báo sắp tới (uint16 big-endian, mét)
 * 12      1     speedLimit của biển báo sắp tới đó
 * 13-14   2     khoảng cách tới camera (uint16 big-endian, mét)
 * 15      1     nhiệt độ hôm nay (int8, °C) — chỉ hợp lệ nếu bit5
 * 16      1     điều kiện thời tiết hôm nay (0=nắng,1=mây,2=mưa,3=giông,4=tuyết/sương)
 * 17      1     nhiệt độ ngày mai (int8, °C) — chỉ hợp lệ nếu bit6
 * 18      1     điều kiện thời tiết ngày mai (cùng thang trên)
 * 19      1     XOR của byte 0..18
 */
object VmsxFrame {
    fun build(
        speedLimit: Int,
        currentSpeed: Int,
        minSpeedLimit: Int = 0,
        navigationState: Int = 0,
        nextLimitDistanceMeters: Int = 0,
        nextLimitSpeedLimit: Int = 0,
        cameraDistanceMeters: Int = 0,
        todayWeatherTempC: Int? = null,
        todayWeatherCondition: Int? = null,
        tomorrowWeatherTempC: Int? = null,
        tomorrowWeatherCondition: Int? = null,
        overSpeed: Boolean = false,
        underMinSpeedLimit: Boolean = false,
        hudConnected: Boolean = false,
    ): ByteArray {
        val hasNextLimit = nextLimitDistanceMeters > 0 || nextLimitSpeedLimit > 0
        val hasCamera = cameraDistanceMeters > 0
        val hasTodayWeather = todayWeatherTempC != null && todayWeatherCondition != null
        val hasTomorrowWeather = tomorrowWeatherTempC != null && tomorrowWeatherCondition != null

        var flags = 0
        if (overSpeed) flags = flags or 0x01
        if (underMinSpeedLimit) flags = flags or 0x02
        if (hudConnected) flags = flags or 0x04
        if (hasNextLimit) flags = flags or 0x08
        if (hasCamera) flags = flags or 0x10
        if (hasTodayWeather) flags = flags or 0x20
        if (hasTomorrowWeather) flags = flags or 0x40

        val nextLimitDist = nextLimitDistanceMeters.coerceIn(0, 0xFFFF)
        val cameraDist = cameraDistanceMeters.coerceIn(0, 0xFFFF)

        val out = ByteArray(20)
        out[0] = 'V'.code.toByte()
        out[1] = 'M'.code.toByte()
        out[2] = 'S'.code.toByte()
        out[3] = 'X'.code.toByte()
        out[4] = 3
        out[5] = clampByte(speedLimit)
        out[6] = clampByte(currentSpeed)
        out[7] = flags.toByte()
        out[8] = clampByte(minSpeedLimit)
        out[9] = clampByte(navigationState)
        out[10] = ((nextLimitDist shr 8) and 0xFF).toByte()
        out[11] = (nextLimitDist and 0xFF).toByte()
        out[12] = clampByte(nextLimitSpeedLimit)
        out[13] = ((cameraDist shr 8) and 0xFF).toByte()
        out[14] = (cameraDist and 0xFF).toByte()
        out[15] = clampSignedByte(todayWeatherTempC ?: 0)
        out[16] = clampByte(todayWeatherCondition ?: 0)
        out[17] = clampSignedByte(tomorrowWeatherTempC ?: 0)
        out[18] = clampByte(tomorrowWeatherCondition ?: 0)

        var checksum = 0
        for (i in 0..18) checksum = checksum xor (out[i].toInt() and 0xFF)
        out[19] = checksum.toByte()

        return out
    }

    private fun clampByte(v: Int): Byte = v.coerceIn(0, 255).toByte()

    private fun clampSignedByte(v: Int): Byte = v.coerceIn(-128, 127).toByte()
}
