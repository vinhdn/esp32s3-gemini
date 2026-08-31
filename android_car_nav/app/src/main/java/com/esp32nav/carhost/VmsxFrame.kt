package com.esp32nav.carhost

/**
 * Dựng frame VMSX y hệt định dạng mà VmslRelay.smali (patch VietMap Live)
 * đã gửi qua BLE cho ESP32 — dùng lại ở đây để nguồn dữ liệu khác (đọc từ
 * bong bóng qua AccessibilityService) vẫn hiển thị được trên board bằng
 * đúng UI/parser (waze_hud_ble.c) đã có sẵn, không cần sửa firmware.
 *
 * offset  size  nội dung
 * 0       4     ASCII "VMSX"
 * 4       1     version = 1
 * 5       1     speedLimit
 * 6       1     currentSpeed
 * 7       1     flags: bit0 overSpeed, bit1 underMinSpeedLimit,
 *                      bit2 hudConnected, bit3 cảnh báo phía trước hợp lệ
 * 8       1     minSpeedLimit
 * 9       1     navigationState
 * 10-11   2     khoảng cách tới cảnh báo (uint16 big-endian, mét)
 * 12      1     speedLimit của cảnh báo đó
 * 13      1     XOR của byte 0..12
 */
object VmsxFrame {
    fun build(
        speedLimit: Int,
        currentSpeed: Int,
        minSpeedLimit: Int = 0,
        navigationState: Int = 0,
        alertDistanceMeters: Int = 0,
        alertSpeedLimit: Int = 0,
        overSpeed: Boolean = false,
        underMinSpeedLimit: Boolean = false,
        hudConnected: Boolean = false,
    ): ByteArray {
        val hasAlert = alertDistanceMeters > 0 || alertSpeedLimit > 0
        var flags = 0
        if (overSpeed) flags = flags or 0x01
        if (underMinSpeedLimit) flags = flags or 0x02
        if (hudConnected) flags = flags or 0x04
        if (hasAlert) flags = flags or 0x08

        val dist = alertDistanceMeters.coerceIn(0, 0xFFFF)
        val out = ByteArray(14)
        out[0] = 'V'.code.toByte()
        out[1] = 'M'.code.toByte()
        out[2] = 'S'.code.toByte()
        out[3] = 'X'.code.toByte()
        out[4] = 1
        out[5] = clampByte(speedLimit)
        out[6] = clampByte(currentSpeed)
        out[7] = flags.toByte()
        out[8] = clampByte(minSpeedLimit)
        out[9] = clampByte(navigationState)
        out[10] = ((dist shr 8) and 0xFF).toByte()
        out[11] = (dist and 0xFF).toByte()
        out[12] = clampByte(alertSpeedLimit)

        var checksum = 0
        for (i in 0..12) checksum = checksum xor (out[i].toInt() and 0xFF)
        out[13] = checksum.toByte()

        return out
    }

    private fun clampByte(v: Int): Byte = v.coerceIn(0, 255).toByte()
}
