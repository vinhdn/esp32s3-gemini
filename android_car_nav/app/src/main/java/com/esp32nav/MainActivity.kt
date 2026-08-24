package com.esp32nav

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import com.esp32nav.model.BleConnectionState
import com.esp32nav.ui.screens.MainScreen
import com.esp32nav.ui.theme.CarNavTheme
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach

class MainActivity : ComponentActivity() {

    private val app by lazy { application as CarNavApplication }
    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val allGranted = results.values.all { it }
        if (allGranted) {
            startBle()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            CarNavTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    MainScreen(
                        bleManager = app.bleManager,
                        obdManager = app.obdManager,
                        application = app,
                        onRequestPermissions = { requestBlePermissions() }
                    )
                }
            }
        }

        checkAndRequestPermissions()
        observeBleState()
    }

    private fun observeBleState() {
        app.bleManager.bleState.onEach { state ->
            val status = when (state.connectionState) {
                BleConnectionState.CONNECTED -> "Connected to ${state.deviceName}"
                BleConnectionState.CONNECTING -> "Connecting..."
                BleConnectionState.SCANNING -> "Scanning..."
                BleConnectionState.DISCONNECTED -> "Disconnected"
            }
            app.updateForegroundServiceStatus(status)
        }.launchIn(scope)

        app.bleManager.receivedMessages.onEach { msg ->
            app.addLogEntry("RX", msg)
        }.launchIn(scope)
    }

    private fun checkAndRequestPermissions() {
        val needed = getRequiredPermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isEmpty()) {
            startBle()
        } else {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }

    private fun requestBlePermissions() {
        val needed = getRequiredPermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isNotEmpty()) {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }

    private fun getRequiredPermissions(): List<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        return permissions
    }

    private fun startBle() {
        app.startForegroundService()
        app.bleManager.startScan()
    }

    override fun onDestroy() {
        super.onDestroy()
        scope.cancel()
    }
}
