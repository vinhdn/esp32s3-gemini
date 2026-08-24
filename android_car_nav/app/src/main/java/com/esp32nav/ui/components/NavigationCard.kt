package com.esp32nav.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.esp32nav.model.NavigationData

@Composable
fun NavigationCard(
    navData: NavigationData?,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer
        )
    ) {
        if (navData == null || !navData.isValid()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(24.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "No navigation active",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.6f)
                )
            }
        } else {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = getDirectionIcon(navData.direction),
                        contentDescription = navData.direction,
                        modifier = Modifier.size(48.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        if (navData.distance.isNotBlank()) {
                            Text(
                                text = navData.distance,
                                style = MaterialTheme.typography.headlineMedium,
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                        if (navData.road.isNotBlank()) {
                            Text(
                                text = navData.road,
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    }
                    if (navData.eta.isNotBlank()) {
                        Column(horizontalAlignment = Alignment.End) {
                            Text(
                                text = "ETA",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
                            )
                            Text(
                                text = navData.eta,
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    }
                }
                if (navData.instruction.isNotBlank()) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = navData.instruction,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                    )
                }
            }
        }
    }
}

private fun getDirectionIcon(direction: String): ImageVector {
    return when (direction) {
        "turn_left" -> Icons.Filled.ArrowBack
        "turn_right" -> Icons.Filled.ArrowForward
        "slight_left" -> Icons.Filled.ArrowBack
        "slight_right" -> Icons.Filled.ArrowForward
        "sharp_left" -> Icons.Filled.ArrowBack
        "sharp_right" -> Icons.Filled.ArrowForward
        "u_turn" -> Icons.Filled.UTurnLeft
        "straight" -> Icons.Filled.ArrowUpward
        "arrive" -> Icons.Filled.Flag
        "roundabout" -> Icons.Filled.RotateRight
        "merge" -> Icons.Filled.MergeType
        "fork_left" -> Icons.Filled.ForkLeft
        "fork_right" -> Icons.Filled.ForkRight
        else -> Icons.Filled.Navigation
    }
}
