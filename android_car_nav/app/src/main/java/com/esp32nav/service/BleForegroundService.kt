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
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach

class BleForegroundService : Service() {

    companion object {
        private const val TAG = "BleForegroundService"
        const val CHANNEL_ID = "ble_foreground_channel"
        const val NOTIFICATION_ID = 1001
    }

    private val serviceScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        val notification = buildNotification("Scanning for HUD...")
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        Log.i(TAG, "Foreground service created")

        // Service tự observe BLE state → cập nhật notification
        // Scope này sống cùng service, KHÔNG phụ thuộc vào Activity
        observeBleState()
        observeReceivedMessages()

        // Kết nối board (BleManager + ImageRelayBle) CHỈ sống trong service:
        // bắt đầu ở đây, dừng ở onDestroy. Activity/Application không được
        // tự gọi startScan()/connect() nữa — chỉ start/stop service này.
        val app = application as? CarNavApplication
        app?.bleManager?.setAutoReconnect(true)
        app?.bleManager?.startScan()
        app?.imageRelay?.start()
    }

    /**
     * Observe BLE connection state và tự cập nhật notification.
     * Chạy trong serviceScope → chỉ bị cancel khi service destroy.
     */
    private fun observeBleState() {
        val app = application as? CarNavApplication ?: return
        app.bleManager.bleState.onEach { state ->
            val status = when (state.connectionState) {
                BleConnectionState.CONNECTED -> "Connected to ${state.deviceName}"
                BleConnectionState.CONNECTING -> "Connecting..."
                BleConnectionState.SCANNING -> "Scanning..."
                BleConnectionState.DISCONNECTED -> "Disconnected"
            }
            updateNotification(status)
        }.launchIn(serviceScope)
    }

    /**
     * Observe RX messages từ BLE → ghi vào Application log.
     * Trước đây nằm ở Activity scope → mất khi Activity destroy.
     */
    private fun observeReceivedMessages() {
        val app = application as? CarNavApplication ?: return
        app.bleManager.receivedMessages.onEach { msg ->
            app.addLogEntry("RX", msg)
        }.launchIn(serviceScope)
    }

    private fun updateNotification(status: String) {
        val notification = buildNotification(status)
        val nm = getSystemService(NotificationManager::class.java) ?: return
        nm.notify(NOTIFICATION_ID, notification)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val status = intent?.getStringExtra("status")

        if (status != null) {
            // Cập nhật notification text (legacy path, giữ lại cho compatibility)
            updateNotification(status)
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
        serviceScope.cancel()
        val app = application as? CarNavApplication
        app?.bleManager?.disconnect()
        app?.imageRelay?.stop()
        Log.w(TAG, "Foreground service destroyed")
        @Suppress("DEPRECATION")
        stopForeground(true) // Compatible with API 28+ (STOP_FOREGROUND_REMOVE is API 33+)
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
        val nm = getSystemService(NotificationManager::class.java) ?: return
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
