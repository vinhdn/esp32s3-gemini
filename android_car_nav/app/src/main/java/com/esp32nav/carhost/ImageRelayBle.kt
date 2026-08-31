package com.esp32nav.carhost

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.esp32nav.model.BleConnectionState
import com.esp32nav.model.BleState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID

private const val TAG = "ImageRelayBle"

/**
 * Kết nối BLE RIÊNG (không dùng chung với relay VMSL/VMSX của VietMap Live
 * đã patch) tới cùng board ESP32, chỉ để gửi frame JPEG cho img_stream.c.
 * NimBLE hiện cấu hình CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3 nên board nhận
 * đồng thời cả hai kết nối trung tâm không xung đột.
 *
 * Khác BleManager.kt (giao thức HLP cũ, service UUID 8a7e0001-...) — class
 * này nói thẳng service/characteristic hiện tại của waze_hud_ble.c
 * (0000ffff-.../00009abc-...) và KHÔNG cần handshake hi/dev: firmware chấp
 * nhận ghi ngay sau khi discover service, ảnh JPEG không cần ACK.
 */
@SuppressLint("MissingPermission")
class ImageRelayBle(private val context: Context) {

    companion object {
        const val TARGET_DEVICE_NAME = "VIETMAP_HUD_H50"
        private const val REQUEST_MTU = 247
        private val SERVICE_UUID = UUID.fromString("0000ffff-0000-1000-8000-00805f9b34fb")
        private val WRITE_CHAR_UUID = UUID.fromString("00009abc-0000-1000-8000-00805f9b34fb")
        // Board cạn ACL/mbuf buffer khi ghi dồn dập cùng lúc với relay
        // VMSL/VMSX (xem ghi chú trong CarHostForegroundService) — tăng delay
        // để board có thời gian giải phóng buffer giữa các chunk.
        private const val INTER_CHUNK_DELAY_MS = 15L
    }

    @Volatile
    var isConnected: Boolean = false
        private set

    // MainActivity trước đây hiển thị trạng thái kết nối board qua
    // bleManager.bleState (BleManager, giao thức HLP cũ) — nhưng
    // BleManager quét tên "VIETMAP_HUD_H1X" trong khi firmware advertise
    // "VIETMAP_HUD_H50" (xem waze_hud_ble.h), nên KHÔNG BAO GIỜ khớp được,
    // UI luôn kẹt ở Scanning/Disconnected dù ImageRelayBle (đúng tên H50)
    // đã connected và đang gửi data thật. UI giờ đọc trạng thái từ đây.
    private val _bleState = MutableStateFlow(BleState())
    val bleState: StateFlow<BleState> = _bleState.asStateFlow()

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter get() = bluetoothManager.adapter
    private val scanner get() = bluetoothAdapter?.bluetoothLeScanner

    private var gatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    private var currentMtu = 23
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var wanted = false

    fun start() {
        wanted = true
        startScan()
    }

    fun stop() {
        wanted = false
        mainHandler.removeCallbacksAndMessages(null)
        try {
            scanner?.stopScan(scanCallback)
        } catch (_: Exception) {}
        gatt?.close()
        gatt = null
        writeCharacteristic = null
        isConnected = false
        isConnecting = false
        _bleState.value = BleState(connectionState = BleConnectionState.DISCONNECTED)
    }

    private fun startScan() {
        if (!wanted) return
        val s = scanner ?: run {
            Log.w(TAG, "Không có BluetoothLeScanner, thử lại sau 3s")
            mainHandler.postDelayed({ startScan() }, 3000)
            return
        }
        val filters = listOf(ScanFilter.Builder().setDeviceName(TARGET_DEVICE_NAME).build())
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try {
            s.startScan(filters, settings, scanCallback)
            Log.i(TAG, "startScan for $TARGET_DEVICE_NAME")
            _bleState.value = BleState(connectionState = BleConnectionState.SCANNING)
        } catch (e: Exception) {
            Log.e(TAG, "startScan failed: ${e.message}")
            mainHandler.postDelayed({ startScan() }, 3000)
        }
    }

    // scanner có thể báo cùng 1 device nhiều lần (nhiều gói advertise) TRƯỚC
    // khi stopScan() thực sự có hiệu lực (async) — không có guard này sẽ
    // spawn nhiều connectGatt() đồng thời tới cùng 1 địa chỉ, gây cạn tài
    // nguyên BLE của hệ thống (thấy rõ khi nhiều app/service cùng khởi động
    // scan gần nhau, vd sau khi đổi kiến trúc kết nối chỉ chạy trong Service).
    @Volatile
    private var isConnecting = false

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (isConnecting || isConnected) return
            isConnecting = true
            _bleState.value = BleState(connectionState = BleConnectionState.CONNECTING)
            try {
                scanner?.stopScan(this)
            } catch (_: Exception) {}
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "onScanFailed: $errorCode")
            if (wanted) mainHandler.postDelayed({ startScan() }, 3000)
        }
    }

    private fun connect(device: BluetoothDevice) {
        Log.i(TAG, "Connecting to ${device.address}")
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "GATT connected, requesting MTU")
                    g.requestMtu(REQUEST_MTU)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.w(TAG, "GATT disconnected (status=$status)")
                    isConnected = false
                    isConnecting = false
                    writeCharacteristic = null
                    try { g.close() } catch (_: Exception) {}
                    if (gatt === g) gatt = null
                    _bleState.value = BleState(
                        connectionState = BleConnectionState.DISCONNECTED,
                        lastError = if (status != BluetoothGatt.GATT_SUCCESS) "Disconnected (status $status)" else null
                    )
                    if (wanted) mainHandler.postDelayed({ startScan() }, 2000)
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            currentMtu = if (status == BluetoothGatt.GATT_SUCCESS) mtu else 23
            Log.i(TAG, "MTU=$currentMtu")
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Service discovery failed: $status")
                g.close()
                return
            }
            val service = g.getService(SERVICE_UUID)
            val chr = service?.getCharacteristic(WRITE_CHAR_UUID)
            if (chr == null) {
                Log.e(TAG, "Không tìm thấy service/characteristic FFFF/9ABC")
                g.close()
                return
            }
            writeCharacteristic = chr
            isConnected = true
            _bleState.value = BleState(
                connectionState = BleConnectionState.CONNECTED,
                deviceName = g.device?.name ?: TARGET_DEVICE_NAME,
                mtu = currentMtu
            )
            Log.i(TAG, "Sẵn sàng gửi ảnh qua 0x9ABC")
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            // WRITE_TYPE_NO_RESPONSE nên không cần xử lý gì thêm.
        }
    }

    /**
     * Gửi 1 frame nhỏ (vd VMSL/VMSX, vài chục byte) — cùng cơ chế write với
     * sendJpegFrame nhưng đặt tên rõ nghĩa cho trường hợp không phải ảnh.
     * Firmware (waze_hud_ble.c access_cb) tự phân loại theo byte đầu/magic,
     * không quan tâm hàm nào ở phía Android gọi.
     */
    fun sendRawFrame(bytes: ByteArray) = sendJpegFrame(bytes)

    /**
     * Gửi 1 frame JPEG hoàn chỉnh, chia chunk theo (MTU-3) byte, dùng
     * WRITE_TYPE_NO_RESPONSE để tối đa thông lượng. Gọi từ background
     * thread — hàm block do có delay giữa các chunk.
     */
    fun sendJpegFrame(jpegBytes: ByteArray) {
        val chr = writeCharacteristic ?: return
        val g = gatt ?: return
        if (!isConnected) return

        val chunkSize = (currentMtu - 3).coerceAtLeast(20)
        var offset = 0
        while (offset < jpegBytes.size) {
            val end = (offset + chunkSize).coerceAtMost(jpegBytes.size)
            val chunk = jpegBytes.copyOfRange(offset, end)
            writeChunk(g, chr, chunk)
            offset = end
            if (offset < jpegBytes.size) {
                try {
                    Thread.sleep(INTER_CHUNK_DELAY_MS)
                } catch (_: InterruptedException) {}
            }
        }
    }

    private fun writeChunk(g: BluetoothGatt, chr: BluetoothGattCharacteristic, data: ByteArray) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(chr, data, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
        } else {
            @Suppress("DEPRECATION")
            chr.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION")
            chr.value = data
            @Suppress("DEPRECATION")
            g.writeCharacteristic(chr)
        }
    }
}
