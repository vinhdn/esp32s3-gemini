package com.esp32nav.carhost

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.graphics.Bitmap
import android.media.Image
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.util.Log
import com.esp32nav.MainActivity
import java.io.ByteArrayOutputStream

/**
 * Foreground service tự host CarAppService của VietMap Live để lấy Surface
 * render map (tỷ lệ 16:9, xem CarAppHostSession) ra làm bitmap, letterbox
 * đúng tỷ lệ (không méo hình) lên khung vuông của board, nén JPEG rồi gửi
 * qua BLE (ImageRelayBle) cho ESP32 hiển thị làm NỀN dưới các widget
 * speed/nav hiện có (xem img_stream.c — canvas đặt ở background).
 *
 * Dữ liệu dẫn đường có cấu trúc (NavigationTemplate/RoutingInfo) vẫn được
 * CarAppHostSession lấy qua requestTemplate() nếu có, nhưng hiện luôn null
 * vì VietMap chỉ đẩy setTrip/sendVMLCommonData khi tự coi phiên là Android
 * Auto "thật" — quyết định nằm trong Dart AOT, chưa tìm ra điều kiện kích
 * hoạt từ tầng native. Xem lịch sử hội thoại.
 */
class CarHostForegroundService : Service() {

    companion object {
        private const val TAG = "CarHostForegroundSvc"
        const val CHANNEL_ID = "car_app_host_channel"
        const val NOTIFICATION_ID = 1002

        // Board là 240x240 (BOARD_LCD_H_RES/V_RES trong img_stream.c).
        private const val BOARD_WIDTH = 240
        private const val BOARD_HEIGHT = 240
        private const val JPEG_QUALITY = 25

        // Board chỉ làm nền, không cần mượt. Test thật trên hardware cho
        // thấy 2 kết nối BLE cùng ghi liên tục (H50 relay ~5-10 write/s +
        // ảnh) làm cạn ACL/mbuf pool của NimBLE (RAM nội bộ board vốn đã
        // eo hẹp — xem sdkconfig.defaults) → log "ACL buf alloc failed"/
        // "MBUF alloc stuck" liên tục, frame ảnh không assemble được. Giảm
        // mạnh tốc độ gửi trước khi cân nhắc tăng buffer pool (tốn RAM nội
        // bộ vốn đã căng).
        private const val MIN_FRAME_INTERVAL_MS = 800L // ~1.25 fps
    }

    private var session: CarAppHostSession? = null
    private var imageRelay: ImageRelayBle? = null
    private var frameCount = 0
    private var lastSendAtMs = 0L
    private val mainHandler = Handler(Looper.getMainLooper())

    private var encodeThread: HandlerThread? = null
    private var encodeHandler: Handler? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Đang bind CarAppService của VietMap Live…"))

        encodeThread = HandlerThread("CarHostEncode").also { it.start() }
        encodeHandler = Handler(encodeThread!!.looper)

        val relay = ImageRelayBle(applicationContext)
        imageRelay = relay
        relay.start()

        val s = CarAppHostSession(applicationContext)
        session = s
        s.frameListener = CarAppHostSession.FrameListener { reader ->
            val image = reader.acquireLatestImage()
            if (image != null) {
                frameCount++
                if (frameCount % 60 == 1) {
                    Log.i(TAG, "Frame #$frameCount: ${image.width}x${image.height}, relayConnected=${relay.isConnected}")
                }

                val now = System.currentTimeMillis()
                if (relay.isConnected && now - lastSendAtMs >= MIN_FRAME_INTERVAL_MS) {
                    lastSendAtMs = now
                    // acquireLatestImage() phải close() trước khi ImageReader
                    // cấp buffer tiếp theo — không thể giữ Image sang thread
                    // khác, nên convert sang Bitmap NGAY trên callback thread
                    // rồi mới đẩy việc nén/gửi (chậm hơn) sang encodeHandler.
                    val bitmap = imageToBitmap(image)
                    image.close()
                    encodeHandler?.post { encodeAndSend(bitmap, relay) }
                } else {
                    image.close()
                }
            }
        }
        s.navInfoListener = CarAppHostSession.NavInfoListener { info ->
            Log.i(
                TAG,
                "NavInfo: cue='${info.cue}' road='${info.road}' maneuverType=${info.maneuverType} " +
                    "currentDistance=${info.currentDistanceMeters} junctionImage=${info.hasJunctionImage} " +
                    "remainingDistance=${info.remainingDistanceMeters} remainingTime=${info.remainingTimeSeconds}s",
            )
        }
        s.bind()

        // onAppCreate/handshake là async (IOnDoneCallback); đợi một nhịp rồi
        // mới start/resume session. Nếu log không thấy "onSurfaceAvailable"
        // trong vài giây, tăng delay này lên hoặc chuyển sang gọi start()
        // ngay trong onSuccess của onAppCreate thay vì đợi cố định.
        mainHandler.postDelayed({ session?.start() }, 1500)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        session?.unbind()
        session = null
        imageRelay?.stop()
        imageRelay = null
        encodeThread?.quitSafely()
        encodeThread = null
        encodeHandler = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun imageToBitmap(image: Image): Bitmap {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowPadding = rowStride - pixelStride * image.width

        val strideBitmap = Bitmap.createBitmap(
            image.width + rowPadding / pixelStride, image.height,
            Bitmap.Config.ARGB_8888,
        )
        strideBitmap.copyPixelsFromBuffer(buffer)
        val bitmap = Bitmap.createBitmap(strideBitmap, 0, 0, image.width, image.height)
        strideBitmap.recycle()
        return bitmap
    }

    private fun encodeAndSend(source: Bitmap, relay: ImageRelayBle) {
        try {
            // KHÔNG còn đệm đen ép về 240x240 vuông ở đây — gửi đúng ảnh tỷ
            // lệ 16:9 thật (không méo, không letterbox phía Android). Board
            // (img_stream.c) tự đặt vị trí trên canvas 240x240 (neo đáy,
            // canh giữa ngang) để né vùng 2 vòng tròn speed thay vì bị che.
            val scale = minOf(BOARD_WIDTH.toFloat() / source.width, BOARD_HEIGHT.toFloat() / source.height)
            val scaledW = (source.width * scale).toInt().coerceAtLeast(1)
            val scaledH = (source.height * scale).toInt().coerceAtLeast(1)
            val scaled = if (scaledW == source.width && scaledH == source.height) {
                source
            } else {
                Bitmap.createScaledBitmap(source, scaledW, scaledH, true)
            }
            if (scaled !== source) source.recycle()

            val out = ByteArrayOutputStream(8192)
            scaled.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, out)
            scaled.recycle()

            relay.sendJpegFrame(out.toByteArray())
        } catch (e: Exception) {
            Log.e(TAG, "encodeAndSend failed: ${e.message}")
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "Car App Host", NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(text: String): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("VietMap Car App Host")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
