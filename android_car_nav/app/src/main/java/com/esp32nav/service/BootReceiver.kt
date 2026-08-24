package com.esp32nav.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.esp32nav.CarNavApplication

/**
 * Tự động khởi động app khi thiết bị boot xong.
 * Khởi động foreground service + bắt đầu scan BLE tìm ESP32 board.
 */
class BootReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "BootReceiver"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return

        if (action == Intent.ACTION_BOOT_COMPLETED ||
            action == "android.intent.action.QUICKBOOT_POWERON" ||
            action == Intent.ACTION_MY_PACKAGE_REPLACED) {

            Log.i(TAG, "Boot completed - starting Car Nav BLE service (action=$action)")

            val app = context.applicationContext as? CarNavApplication ?: return

            // Khởi động foreground service để giữ BLE alive
            app.startForegroundService()

            // Bắt đầu scan BLE tìm ESP32 board
            app.bleManager.setAutoReconnect(true)
            app.bleManager.startScan()

            Log.i(TAG, "Foreground service started, BLE scanning for board...")
        }
    }
}
