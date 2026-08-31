package com.esp32nav.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.esp32nav.CarNavApplication
import com.esp32nav.LogEntry
import com.esp32nav.ble.BleManager
import com.esp32nav.model.BleState
import com.esp32nav.model.NavigationData
import com.esp32nav.model.VehicleData
import com.esp32nav.obd.ObdManager
import com.esp32nav.ui.components.ConnectionStatus
import com.esp32nav.ui.components.NavigationCard
import com.esp32nav.ui.components.VehicleDataCard
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(
    bleManager: BleManager,
    obdManager: ObdManager,
    application: CarNavApplication,
    onRequestPermissions: () -> Unit
) {
    // Trạng thái kết nối board hiển thị lấy từ imageRelay (kênh THẬT đang
    // connected + gửi data — service/char 0xFFFF/0x9ABC), KHÔNG phải
    // bleManager.bleState nữa: BleManager quét tên "VIETMAP_HUD_H1X" nhưng
    // firmware advertise "VIETMAP_HUD_H50" nên không bao giờ khớp, làm UI
    // kẹt ở Scanning/Disconnected dù board thực tế đã connected.
    val bleState by application.imageRelay.bleState.collectAsState()
    val navData by application.currentNavData.collectAsState()
    val messageLog by application.messageLog.collectAsState()
    val vehicleData by application.vhalManager.vehicleData.collectAsState()
    val obdVehicleData by obdManager.vehicleData.collectAsState()
    val obdState by obdManager.connectionState.collectAsState()
    val obdDeviceName by obdManager.connectedDeviceName.collectAsState()
    val vhalState by application.vhalManager.connectionState.collectAsState()
    val activeSource by application.activeVehicleSource.collectAsState()
    var showSettings by remember { mutableStateOf(false) }

    // Chọn vehicle data hiển thị dựa trên source nào đang active
    val displayVehicleData = when (activeSource) {
        "vhal" -> vehicleData
        "obd" -> obdVehicleData
        else -> VehicleData()
    }

    if (showSettings) {
        SettingsScreen(
            bleManager = bleManager,
            obdManager = obdManager,
            onBack = { showSettings = false }
        )
    } else {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("Car Nav BLE") },
                    actions = {
                        IconButton(onClick = { showSettings = true }) {
                            Icon(Icons.Filled.Settings, contentDescription = "Settings")
                        }
                    }
                )
            }
        ) { padding ->
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .padding(horizontal = 16.dp)
            ) {
                Spacer(modifier = Modifier.height(8.dp))

                ConnectionStatus(
                    bleState = bleState,
                    // Kết nối board chỉ sống trong BleForegroundService — Activity/
                    // Compose không gọi bleManager.startScan()/disconnect() trực
                    // tiếp nữa, chỉ start/stop service (service tự lo phần kết nối
                    // trong onCreate/onDestroy).
                    onConnect = { application.startForegroundService() },
                    onDisconnect = { application.stopForegroundService() }
                )

                Spacer(modifier = Modifier.height(8.dp))

                // Vehicle data source status
                VehicleSourceRow(vhalState, obdState, activeSource, obdDeviceName)

                Spacer(modifier = Modifier.height(12.dp))

                // Vehicle data (nếu có)
                if (displayVehicleData.hasData()) {
                    VehicleDataCard(vehicleData = displayVehicleData)
                    Spacer(modifier = Modifier.height(8.dp))
                }

                // Navigation (nếu có)
                if (navData != null) {
                    Text(
                        text = "Navigation",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(vertical = 4.dp)
                    )
                    NavigationCard(navData = navData)
                    Spacer(modifier = Modifier.height(8.dp))
                }

                Text(
                    text = "Message Log",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 4.dp)
                )

                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface
                    )
                ) {
                    if (messageLog.isEmpty()) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(16.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "No messages yet",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
                            )
                        }
                    } else {
                        LazyColumn(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(8.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            items(messageLog) { entry ->
                                MessageLogItem(entry)
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))
            }
        }
    }
}

@Composable
private fun VehicleSourceRow(
    vhalState: com.esp32nav.vhal.VhalManager.VhalConnectionState,
    obdState: ObdManager.ObdConnectionState,
    activeSource: String,
    obdDeviceName: String
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        val (statusText, color) = when (activeSource) {
            "vhal" -> "🚗 VHAL: Connected (Car API)" to MaterialTheme.colorScheme.primary
            "obd" -> "🔌 OBD: $obdDeviceName" to MaterialTheme.colorScheme.tertiary
            else -> {
                val detail = when {
                    vhalState == com.esp32nav.vhal.VhalManager.VhalConnectionState.CONNECTING -> "VHAL connecting..."
                    obdState == ObdManager.ObdConnectionState.CONNECTING -> "OBD connecting..."
                    else -> "No vehicle data source"
                }
                detail to MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
            }
        }
        Text(
            text = statusText,
            style = MaterialTheme.typography.bodySmall,
            color = color
        )
    }
}

@Composable
private fun MessageLogItem(entry: LogEntry) {
    val timeFormat = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }
    val time = timeFormat.format(Date(entry.timestamp))
    val dirColor = if (entry.direction == "TX") {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.tertiary
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Top
    ) {
        Text(
            text = time,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
            fontFamily = FontFamily.Monospace
        )
        Spacer(modifier = Modifier.width(6.dp))
        Text(
            text = entry.direction,
            style = MaterialTheme.typography.labelSmall,
            color = dirColor,
            fontFamily = FontFamily.Monospace
        )
        Spacer(modifier = Modifier.width(6.dp))
        Text(
            text = entry.message,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.weight(1f),
            maxLines = 2
        )
    }
}
