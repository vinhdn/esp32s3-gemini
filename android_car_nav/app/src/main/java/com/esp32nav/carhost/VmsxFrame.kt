package com.esp32nav.carhost

/**
 * Dựng frame VMSX y hệt định dạng mà waze_hud_ble.c parse để gửi qua BLE cho
 * ESP32 board.
 *
 * v2 (16 byte) — bong bóng VietMap Live thực tế có 2 khu cảnh báo ĐỘC LẬP
 * (id sq_upcoming_alert_left/right hoặc h_upcoming_alert_left/right, mỗi
 * bên 1 khoảng cách riêng — xác nhận qua dump thật: trái 186m, phải 365m
 * cùng lúc) — v1 (14 byte) chỉ mang 1 khoảng cách chung nên mất dữ liệu 1
 * bên. Quy ước: trái = biển báo tốc độ sắp tới, phải = camera.
 *
 * offset  size  nội dung
 * 0       4     ASCII "VMSX"
 * 1       1     version = 2
 * 5       1     speedLimit (biển báo hiện tại)
 * 6       1     currentSpeed
 * 7       1     flags: bit0 overSpeed, bit1 underMinSpeedLimit,
 *                      bit2 hudConnected, bit3 nextLimit hợp lệ,
 *                      bit4 camera hợp lệ
 * 8       1     minSpeedLimit
 * 9       1     navigationState
 * 10-11   2     khoảng cách tới biển báo sắp tới (uint16 big-endian, mét)
 * 12      1     speedLimit của biển báo sắp tới đó (0 nếu không có số,
 *                kiểu bong bóng "sq_" không lộ số qua accessibility text)
 * 13-14   2     khoảng cách tới camera (uint16 big-endian, mét)
 * 15      1     XOR của byte 0..14
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
        overSpeed: Boolean = false,
        underMinSpeedLimit: Boolean = false,
        hudConnected: Boolean = false,
    ): ByteArray {
        val hasNextLimit = nextLimitDistanceMeters > 0 || nextLimitSpeedLimit > 0
        val hasCamera = cameraDistanceMeters > 0
        var flags = 0
        if (overSpeed) flags = flags or 0x01
        if (underMinSpeedLimit) flags = flags or 0x02
        if (hudConnected) flags = flags or 0x04
        if (hasNextLimit) flags = flags or 0x08
        if (hasCamera) flags = flags or 0x10

        val nextLimitDist = nextLimitDistanceMeters.coerceIn(0, 0xFFFF)
        val cameraDist = cameraDistanceMeters.coerceIn(0, 0xFFFF)

        val out = ByteArray(16)
        out[0] = 'V'.code.toByte()
        out[1] = 'M'.code.toByte()
        out[2] = 'S'.code.toByte()
        out[3] = 'X'.code.toByte()
        out[4] = 2
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

        var checksum = 0
        for (i in 0..14) checksum = checksum xor (out[i].toInt() and 0xFF)
        out[15] = checksum.toByte()

        return out
    }

    private fun clampByte(v: Int): Byte = v.coerceIn(0, 255).toByte()
}
