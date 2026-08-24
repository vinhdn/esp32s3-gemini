package com.esp32nav.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.esp32nav.model.BleConnectionState
import com.esp32nav.model.BleState
import com.esp32nav.ui.theme.*

@Composable
fun ConnectionStatus(
    bleState: BleState,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.weight(1f)
            ) {
                val (icon, color, statusText) = when (bleState.connectionState) {
                    BleConnectionState.CONNECTED -> Triple(
                        Icons.Filled.BluetoothConnected,
                        Green500,
                        "Connected"
                    )
                    BleConnectionState.CONNECTING -> Triple(
                        Icons.Filled.BluetoothSearching,
                        Orange500,
                        "Connecting..."
                    )
                    BleConnectionState.SCANNING -> Triple(
                        Icons.Filled.BluetoothSearching,
                        Orange500,
                        "Scanning..."
                    )
                    BleConnectionState.DISCONNECTED -> Triple(
                        Icons.Filled.BluetoothDisabled,
                        Red500,
                        "Disconnected"
                    )
                }

                Icon(
                    imageVector = icon,
                    contentDescription = statusText,
                    tint = color,
                    modifier = Modifier.size(28.dp)
                )
                Spacer(modifier = Modifier.width(12.dp))
                Column {
                    Text(
                        text = statusText,
                        style = MaterialTheme.typography.titleMedium,
                        color = color
                    )
                    if (bleState.deviceName.isNotBlank() &&
                        bleState.connectionState == BleConnectionState.CONNECTED
                    ) {
                        Text(
                            text = "${bleState.deviceName} (MTU: ${bleState.mtu})",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (bleState.lastError != null) {
                        Text(
                            text = bleState.lastError,
                            style = MaterialTheme.typography.bodySmall,
                            color = Red500
                        )
                    }
                }
            }

            when (bleState.connectionState) {
                BleConnectionState.CONNECTED -> {
                    FilledTonalButton(onClick = onDisconnect) {
                        Text("Disconnect")
                    }
                }
                BleConnectionState.DISCONNECTED -> {
                    Button(onClick = onConnect) {
                        Text("Connect")
                    }
                }
                else -> {
                    CircularProgressIndicator(
                        modifier = Modifier.size(24.dp),
                        strokeWidth = 2.dp
                    )
                }
            }
        }
    }
}
