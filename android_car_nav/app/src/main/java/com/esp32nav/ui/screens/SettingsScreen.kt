package com.esp32nav.ui.screens

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.esp32nav.CarNavApplication
import com.esp32nav.ble.BleManager
import com.esp32nav.carhost.VhudFrame
import com.esp32nav.obd.ObdManager
import com.esp32nav.service.NavigationListenerService

@SuppressLint("MissingPermission")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    bleManager: BleManager,
    obdManager: ObdManager,
    application: CarNavApplication,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var autoReconnect by remember { mutableStateOf(true) }
    val hudPrefs = remember { context.getSharedPreferences("car_nav_prefs", Context.MODE_PRIVATE) }
    var hudFlip by remember { mutableStateOf(hudPrefs.getBoolean("hud_flip", false)) }
    val notificationAccessEnabled = remember { isNotificationAccessEnabled(context) }
    val obdState by obdManager.connectionState.collectAsState()

    var showObdDevices by remember { mutableStateOf(false) }
    val pairedDevices = remember { obdManager.getPairedDevices() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // OBD-II Connection
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "OBD-II (ELM327)",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = when (obdState) {
                                ObdManager.ObdConnectionState.CONNECTED -> "✓ Connected - reading vehicle data"
                                ObdManager.ObdConnectionState.CONNECTING -> "⏳ Connecting to ELM327..."
                                ObdManager.ObdConnectionState.ERROR -> "⚠ Connection error"
                                ObdManager.ObdConnectionState.DISCONNECTED -> "Not connected. Select a paired ELM327 device."
                            },
                            style = MaterialTheme.typography.bodyMedium,
                            color = when (obdState) {
                                ObdManager.ObdConnectionState.CONNECTED -> MaterialTheme.colorScheme.primary
                                ObdManager.ObdConnectionState.ERROR -> MaterialTheme.colorScheme.error
                                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                            }
                        )
                        Spacer(modifier = Modifier.height(12.dp))

                        if (obdState == ObdManager.ObdConnectionState.CONNECTED) {
                            Button(onClick = { obdManager.disconnect() }) {
                                Text("Disconnect OBD")
                            }
                        } else {
                            Button(onClick = { showObdDevices = !showObdDevices }) {
                                Text(if (showObdDevices) "Hide Devices" else "Select ELM327 Device")
                            }
                        }

                        if (showObdDevices && obdState != ObdManager.ObdConnectionState.CONNECTED) {
                            Spacer(modifier = Modifier.height(8.dp))
                            if (pairedDevices.isEmpty()) {
                                Text(
                                    text = "No paired Bluetooth devices found.\nPair your ELM327 in system Bluetooth settings first.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
                                )
                            } else {
                                Text(
                                    text = "Paired devices:",
                                    style = MaterialTheme.typography.labelMedium
                                )
                                pairedDevices.forEach { device ->
                                    ObdDeviceItem(device = device) {
                                        obdManager.connect(device)
                                        showObdDevices = false
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Notification Access
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "Notification Access",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = if (notificationAccessEnabled) {
                                "✓ Enabled. Reading Google Maps + DatMap notifications."
                            } else {
                                "⚠ Required to read navigation + DatMap speed limit notifications."
                            },
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (notificationAccessEnabled) {
                                MaterialTheme.colorScheme.primary
                            } else {
                                MaterialTheme.colorScheme.error
                            }
                        )
                        Spacer(modifier = Modifier.height(12.dp))
                        Button(onClick = {
                            val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
                            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            context.startActivity(intent)
                        }) {
                            Text("Open Notification Settings")
                        }
                    }
                }
            }

            // DatMap / Accessibility
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "DatMap - Tốc độ giới hạn",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Đọc tốc độ giới hạn từ bubble overlay của DatMap qua Accessibility Service.\n\n" +
                                    "Cần bật cả 2:\n" +
                                    "• Notification Access (đọc cảnh báo camera)\n" +
                                    "• Accessibility Service (đọc bubble tốc độ giới hạn)",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                        )
                        Spacer(modifier = Modifier.height(12.dp))
                        Button(onClick = {
                            val intent = Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            context.startActivity(intent)
                        }) {
                            Text("Open Accessibility Settings")
                        }
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Tìm \"Car Nav BLE\" trong danh sách Accessibility → Bật",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
                        )
                    }
                }
            }

            // Auto-reconnect
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Auto-reconnect", style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Reconnect to HUD when BLE disconnected",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                            )
                        }
                        Switch(
                            checked = autoReconnect,
                            onCheckedChange = {
                                autoReconnect = it
                                bleManager.setAutoReconnect(it)
                            }
                        )
                    }
                }
            }

            // HUD (lật màn hình cho kính lái)
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Chế độ HUD (kính lái)", style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Lật gương (trái-phải) trên board — đặt board trên táp-lô, " +
                                    "ảnh phản chiếu qua kính lái sẽ hiện đúng chiều",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                            )
                        }
                        Switch(
                            checked = hudFlip,
                            onCheckedChange = {
                                hudFlip = it
                                hudPrefs.edit().putBoolean("hud_flip", it).apply()
                                application.imageRelay.sendRawFrame(VhudFrame.build(it))
                            }
                        )
                    }
                }
            }

            // Device info
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("BLE HUD Device", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))
                        InfoRow("Device name", BleManager.TARGET_DEVICE_NAME)
                        InfoRow("Protocol", "HLP/1 (JSON Lines)")
                        InfoRow("Messages", "nav, veh (speed/temp/tires)")
                    }
                }
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun ObdDeviceItem(device: BluetoothDevice, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(vertical = 8.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = device.name ?: "Unknown",
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = device.address,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
            )
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
        )
        Text(text = value, style = MaterialTheme.typography.bodyMedium)
    }
}

private fun isNotificationAccessEnabled(context: Context): Boolean {
    val cn = ComponentName(context, NavigationListenerService::class.java)
    val flat = Settings.Secure.getString(context.contentResolver, "enabled_notification_listeners")
    return flat != null && flat.contains(cn.flattenToString())
}
