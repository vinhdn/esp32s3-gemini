package com.esp32nav.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import android.util.Log
import com.esp32nav.model.BleConnectionState
import com.esp32nav.model.BleState
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.util.*
import java.util.concurrent.ConcurrentLinkedQueue

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    companion object {
        private const val TAG = "BleManager"
        const val TARGET_DEVICE_NAME = "VIETMAP_HUD_H1X"
        private const val REQUEST_MTU = 247
        private val SERVICE_UUID = UUID.fromString("8a7e0001-4d6e-4c48-9a9d-484c504c0001")
        private val TX_CHAR_UUID = UUID.fromString("8a7e0002-4d6e-4c48-9a9d-484c504c0001")
        private val RX_CHAR_UUID = UUID.fromString("8a7e0003-4d6e-4c48-9a9d-484c504c0001")
        private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter
    private val scanner: BluetoothLeScanner? get() = bluetoothAdapter?.bluetoothLeScanner

    private var gatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var currentMtu = 23
    private var isWriting = false
    private val writeQueue = ConcurrentLinkedQueue<ByteArray>()

    private val _bleState = MutableStateFlow(BleState())
    val bleState: StateFlow<BleState> = _bleState.asStateFlow()

    private val _receivedMessages = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val receivedMessages: SharedFlow<String> = _receivedMessages.asSharedFlow()

    private val _sentMessages = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val sentMessages: SharedFlow<String> = _sentMessages.asSharedFlow()

    private var autoReconnect = true
    private var reconnectJob: Job? = null
    private var reconnectAttempt = 0
    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    fun setAutoReconnect(enabled: Boolean) {
        autoReconnect = enabled
        if (!enabled) {
            reconnectJob?.cancel()
            reconnectJob = null
        }
    }

    fun startScan() {
        val s = scanner
        if (s == null) {
            _bleState.value = _bleState.value.copy(lastError = "Bluetooth not available")
            return
        }

        reconnectJob?.cancel()
        reconnectAttempt = 0
        _bleState.value = _bleState.value.copy(
            connectionState = BleConnectionState.SCANNING,
            lastError = null
        )

        val filters = listOf(
            ScanFilter.Builder().setDeviceName(TARGET_DEVICE_NAME).build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        s.startScan(filters, settings, scanCallback)

        scope.launch {
            delay(15000)
            if (_bleState.value.connectionState == BleConnectionState.SCANNING) {
                stopScan()
                _bleState.value = _bleState.value.copy(
                    connectionState = BleConnectionState.DISCONNECTED,
                    lastError = "Scan timeout - device not found"
                )
                scheduleReconnect()
            }
        }
    }

    fun stopScan() {
        scanner?.stopScan(scanCallback)
    }

    fun disconnect() {
        autoReconnect = false
        reconnectJob?.cancel()
        performDisconnect()
    }

    private fun performDisconnect() {
        val g = gatt
        if (g != null && txCharacteristic != null) {
            val byeData = HlpProtocol.createByeMessage().toByteArray(Charsets.UTF_8)
            writeDataInternal(byeData)
            scope.launch {
                delay(200)
                g.close()
                gatt = null
                txCharacteristic = null
                rxCharacteristic = null
                _bleState.value = _bleState.value.copy(
                    connectionState = BleConnectionState.DISCONNECTED
                )
            }
        } else {
            g?.close()
            gatt = null
            txCharacteristic = null
            rxCharacteristic = null
            _bleState.value = _bleState.value.copy(
                connectionState = BleConnectionState.DISCONNECTED
            )
        }
    }

    fun sendNavigation(data: com.esp32nav.model.NavigationData) {
        if (_bleState.value.connectionState != BleConnectionState.CONNECTED) return
        val message = HlpProtocol.createNavMessage(data)
        writeData(message)
    }

    fun writeData(message: String) {
        val bytes = message.toByteArray(Charsets.UTF_8)
        writeDataInternal(bytes)
        _sentMessages.tryEmit(message.trim())
    }

    private fun writeDataInternal(data: ByteArray) {
        val chunks = HlpProtocol.chunkData(data, currentMtu)
        for (chunk in chunks) {
            writeQueue.add(chunk)
        }
        processWriteQueue()
    }

    private fun processWriteQueue() {
        if (isWriting) return
        val chunk = writeQueue.poll() ?: return
        val tx = txCharacteristic ?: return
        val g = gatt ?: return

        isWriting = true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(tx, chunk, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        } else {
            @Suppress("DEPRECATION")
            tx.value = chunk
            @Suppress("DEPRECATION")
            tx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            g.writeCharacteristic(tx)
        }
    }

    private fun scheduleReconnect() {
        if (!autoReconnect) return
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            val delayMs = minOf(1000L * (1L shl minOf(reconnectAttempt, 5)), 32000L)
            reconnectAttempt++
            Log.d(TAG, "Reconnecting in ${delayMs}ms (attempt $reconnectAttempt)")
            delay(delayMs)
            startScan()
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            connectToDevice(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "Scan failed: $errorCode")
            _bleState.value = _bleState.value.copy(
                connectionState = BleConnectionState.DISCONNECTED,
                lastError = "Scan failed (error $errorCode)"
            )
            scheduleReconnect()
        }
    }

    private fun connectToDevice(device: BluetoothDevice) {
        _bleState.value = _bleState.value.copy(
            connectionState = BleConnectionState.CONNECTING,
            deviceName = device.name ?: TARGET_DEVICE_NAME
        )
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.d(TAG, "Connected, requesting MTU")
                    gatt.requestMtu(REQUEST_MTU)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.d(TAG, "Disconnected")
                    this@BleManager.gatt?.close()
                    this@BleManager.gatt = null
                    txCharacteristic = null
                    rxCharacteristic = null
                    writeQueue.clear()
                    isWriting = false
                    _bleState.value = _bleState.value.copy(
                        connectionState = BleConnectionState.DISCONNECTED,
                        lastError = if (status != BluetoothGatt.GATT_SUCCESS) "Disconnected (status $status)" else null
                    )
                    scheduleReconnect()
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            currentMtu = if (status == BluetoothGatt.GATT_SUCCESS) mtu else 23
            Log.d(TAG, "MTU changed to $currentMtu")
            _bleState.value = _bleState.value.copy(mtu = currentMtu)
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                _bleState.value = _bleState.value.copy(lastError = "Service discovery failed")
                gatt.close()
                return
            }

            val service = gatt.getService(SERVICE_UUID)
            if (service == null) {
                _bleState.value = _bleState.value.copy(lastError = "HLP service not found")
                gatt.close()
                return
            }

            txCharacteristic = service.getCharacteristic(TX_CHAR_UUID)
            rxCharacteristic = service.getCharacteristic(RX_CHAR_UUID)

            if (txCharacteristic == null || rxCharacteristic == null) {
                _bleState.value = _bleState.value.copy(lastError = "Required characteristics not found")
                gatt.close()
                return
            }

            // Enable notifications on RX
            gatt.setCharacteristicNotification(rxCharacteristic, true)
            val descriptor = rxCharacteristic!!.getDescriptor(CCCD_UUID)
            if (descriptor != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    @Suppress("DEPRECATION")
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    gatt.writeDescriptor(descriptor)
                }
            } else {
                onReady()
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid == CCCD_UUID) {
                onReady()
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            handleRxData(value)
        }

        @Suppress("DEPRECATION")
        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            handleRxData(characteristic.value)
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            isWriting = false
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Write failed: $status")
            }
            processWriteQueue()
        }
    }

    private fun onReady() {
        reconnectAttempt = 0
        _bleState.value = _bleState.value.copy(
            connectionState = BleConnectionState.CONNECTED,
            lastError = null
        )
        // Send hi message
        writeData(HlpProtocol.createHiMessage())
    }

    private val rxBuffer = StringBuilder()

    private fun handleRxData(data: ByteArray) {
        val text = String(data, Charsets.UTF_8)
        rxBuffer.append(text)

        while (true) {
            val newlineIdx = rxBuffer.indexOf('\n')
            if (newlineIdx < 0) break
            val line = rxBuffer.substring(0, newlineIdx)
            rxBuffer.delete(0, newlineIdx + 1)

            if (line.isBlank()) continue
            _receivedMessages.tryEmit(line)

            val msg = HlpProtocol.parseMessage(line)
            if (msg != null) {
                handleHlpMessage(msg)
            }
        }
    }

    private fun handleHlpMessage(msg: HlpMessage) {
        when (msg.type) {
            "ping" -> {
                writeData(HlpProtocol.createPongMessage())
            }
            "dev" -> {
                Log.d(TAG, "Device info: ${msg.payload}")
            }
        }
    }

    fun destroy() {
        reconnectJob?.cancel()
        scope.cancel()
        stopScan()
        gatt?.close()
        gatt = null
    }
}
