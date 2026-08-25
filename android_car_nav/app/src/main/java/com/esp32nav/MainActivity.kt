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
import com.esp32nav.ui.screens.MainScreen
import com.esp32nav.ui.theme.CarNavTheme

/**
 * MainActivity chỉ hiển thị UI.
 *
 * Mọi logic BLE (scan, connect, observe state, update notification)
 * đã được chuyển sang BleForegroundService + CarNavApplication.
 * Activity có thể bị destroy/recreate bất cứ lúc nào mà không ảnh hưởng
 * đến kết nối BLE hay foreground service notification.
 */
class MainActivity : ComponentActivity() {

    private val app by lazy { application as CarNavApplication }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val allGranted = results.values.all { it }
        if (allGranted) {
            // Permissions granted → đảm bảo foreground service đang chạy
            // BLE scan đã được Application/BootReceiver quản lý, không cần gọi lại ở đây
            app.startForegroundService()
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
    }

    private fun checkAndRequestPermissions() {
        val needed = getRequiredPermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isEmpty()) {
            // Đã có quyền, đảm bảo service chạy (idempotent nếu đã chạy)
            app.startForegroundService()
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
}
