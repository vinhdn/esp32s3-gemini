package com.esp32nav.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.esp32nav.model.VehicleData

@Composable
fun VehicleDataCard(
    vehicleData: VehicleData,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.secondaryContainer
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp)
        ) {
            // Speed + RPM row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.Bottom
            ) {
                // Speed
                Column {
                    if (vehicleData.speedKmh >= 0) {
                        Text(
                            text = "${vehicleData.speedKmh}",
                            style = MaterialTheme.typography.headlineLarge.copy(
                                fontWeight = FontWeight.Bold,
                                fontSize = 36.sp
                            ),
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                        Text(
                            text = "km/h",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                        )
                    }
                }
                // RPM
                if (vehicleData.rpm >= 0) {
                    Column(horizontalAlignment = Alignment.End) {
                        Text(
                            text = "${vehicleData.rpm}",
                            style = MaterialTheme.typography.titleLarge,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                        Text(
                            text = "RPM",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                        )
                    }
                }
            }

            // Temperatures row
            val temps = buildList {
                if (vehicleData.coolantTempC > -999) add("Nước: ${vehicleData.coolantTempC}°C")
                if (vehicleData.oilTempC > -999) add("Dầu: ${vehicleData.oilTempC}°C")
                if (vehicleData.intakeTempC > -999) add("Khí nạp: ${vehicleData.intakeTempC}°C")
            }
            if (temps.isNotEmpty()) {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = temps.joinToString("  "),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.8f)
                )
            }

            // Tire pressure row
            val hasTires = vehicleData.tireFLkPa >= 0 || vehicleData.tireFRkPa >= 0 ||
                    vehicleData.tireRLkPa >= 0 || vehicleData.tireRRkPa >= 0
            if (hasTires) {
                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    TirePressureItem("FL", vehicleData.tireFLkPa)
                    TirePressureItem("FR", vehicleData.tireFRkPa)
                    TirePressureItem("RL", vehicleData.tireRLkPa)
                    TirePressureItem("RR", vehicleData.tireRRkPa)
                }
            }
        }
    }
}

@Composable
private fun TirePressureItem(label: String, kpa: Int) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.6f)
        )
        Text(
            text = if (kpa >= 0) "${kpa}kPa" else "--",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSecondaryContainer
        )
    }
}
