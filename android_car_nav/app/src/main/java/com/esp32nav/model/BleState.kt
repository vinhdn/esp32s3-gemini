package com.esp32nav.model

enum class BleConnectionState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED
}

data class BleState(
    val connectionState: BleConnectionState = BleConnectionState.DISCONNECTED,
    val deviceName: String = "",
    val mtu: Int = 23,
    val lastError: String? = null
)
