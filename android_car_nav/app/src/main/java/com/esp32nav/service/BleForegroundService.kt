package com.esp32nav.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.esp32nav.CarNavApplication
import com.esp32nav.MainActivity
import com.esp32nav.model.BleConnectionState

class BleForegroundService : Service() {

    companion object {
        private const val TAG = "BleForegroundService"
        const val CHANNEL_ID = "ble_foreground_channel"
        const val NOTIFICATION_ID = 1001
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        val notification = buildNotification("Scanning for HUD...")
        startForeground(NOTIFICATION_ID, notification)
        Log.i(TAG, "Foreground service created")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val status = intent?.getStringExtra("status")

        if (status != null) {
            // Cập nhật notification text
            val notification = buildNotification(status)
            val nm = getSystemService(NotificationManager::class.java)
            nm.notify(NOTIFICATION_ID, notification)
        } else {
            // Service restart (sau khi bị kill) - tự động scan lại BLE
            Log.i(TAG, "Service restarted, auto-reconnecting BLE...")
            val app = application as? CarNavApplication
            if (app != null) {
                val state = app.bleManager.bleState.value.connectionState
                if (state == BleConnectionState.DISCONNECTED) {
                    app.bleManager.setAutoReconnect(true)
                    app.bleManager.startScan()
                }
            }
        }

        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        super.onDestroy()
        Log.w(TAG, "Foreground service destroyed")
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "BLE Connection",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Keeps BLE connection to HUD alive"
            setShowBadge(false)
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(channel)
    }

    private fun buildNotification(status: String): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Car Nav BLE")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
