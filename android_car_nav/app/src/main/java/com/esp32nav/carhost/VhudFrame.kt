package com.esp32nav.carhost

/**
 * Frame lệnh "VHUD" (7 byte) — bật/tắt chế độ HUD (lật màn hình 180 độ để
 * phản chiếu lên kính lái đúng chiều) trên board, xem waze_hud_ble.c. Gửi
 * khi người dùng bật/tắt công tắc trong Settings — KHÔNG phải frame liên
 * tục như VmsxFrame, chỉ gửi khi đổi trạng thái. Board tự lưu vào NVS nên
 * không cần gửi lại mỗi lần kết nối.
 *
 * offset  size  nội dung
 * 0       4     ASCII "VHUD"
 * 4       1     version = 1
 * 5       1     flipped (0/1)
 * 6       1     XOR của byte 0..5
 */
object VhudFrame {
    fun build(flipped: Boolean): ByteArray {
        val out = ByteArray(7)
        out[0] = 'V'.code.toByte()
        out[1] = 'H'.code.toByte()
        out[2] = 'U'.code.toByte()
        out[3] = 'D'.code.toByte()
        out[4] = 1
        out[5] = if (flipped) 1 else 0

        var checksum = 0
        for (i in 0..5) checksum = checksum xor (out[i].toInt() and 0xFF)
        out[6] = checksum.toByte()

        return out
    }
}
