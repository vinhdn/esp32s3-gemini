package com.esp32nav

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import com.esp32nav.stream.MapStreamManager
import com.esp32nav.ui.screens.MainScreen
import com.esp32nav.ui.theme.CarNavTheme

class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "MainActivity"
    }

    private val app by lazy { application as CarNavApplication }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val allGranted = results.values.all { it }
        if (allGranted) {
            app.startForegroundService()
        }
    }

    // Screen capture permission launcher
    private val screenCaptureLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        Log.i(TAG, "Screen capture result: ${result.resultCode}")
        if (result.resultCode == RESULT_OK) {
            // Android 14+: foreground service (mediaProjection type) PHẢI chạy
            // trước khi getMediaProjection. Start FGS rồi delay nhỏ.
            app.startForegroundService()
            window.decorView.postDelayed({
                app.mapStreamManager.onPermissionResult(result.resultCode, result.data)
                app.startMapStream()
                Log.i(TAG, "Map streaming started!")
            }, 500)
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
            app.startForegroundService()
        } else {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }

    // Đã dừng dùng MediaProjection (chụp/crop màn hình) — chuyển sang đọc
    // dữ liệu bong bóng qua VietmapAccessibilityService (accessibility node
    // tree) thay vì chụp bitmap. Giữ lại screenCaptureLauncher/MapStreamManager
    // trong code (không xoá) phòng cần dùng lại, nhưng không còn tự kích hoạt.

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
