package com.esp32nav.stream

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.Image
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.HandlerThread
import android.util.DisplayMetrics
import android.util.Log
import android.view.WindowManager
import com.esp32nav.carhost.ImageRelayBle
import com.esp32nav.service.VietmapAccessibilityService
import java.io.ByteArrayOutputStream

/**
 * Chụp bong bóng nổi (floating bubble) của VietMap Live qua MediaProjection,
 * crop đúng vùng bong bóng (tọa độ lấy từ VietmapAccessibilityService — xem
 * updateBubbleBounds()), giữ nguyên tỷ lệ (không méo), gửi JPEG qua BLE
 * (ImageRelayBle) cho ESP32 hiển thị.
 *
 * Vì sao chụp bitmap thay vì đọc accessibility text: bong bóng hiện có 4
 * dữ liệu (biển giới hạn tốc độ hiện tại DẠNG ẢNH, tốc độ hiện tại dạng số,
 * biển giới hạn tiếp theo DẠNG ẢNH, khoảng cách camera) — 2 trong 4 là ảnh
 * biển báo, accessibility node text/contentDescription không đọc được nội
 * dung ảnh, nên chụp nguyên bitmap là cách nhanh và đầy đủ nhất.
 *
 * BLE frame protocol: xem ImageRelayBle/img_stream.c trên ESP32 — JPEG thô
 * chia chunk theo MTU, ESP32 tự nhận biết SOI (0xFFD8)/EOI (0xFFD9).
 */
class MapStreamManager(
    private val context: Context,
    private val imageRelay: ImageRelayBle,
) {
    companion object {
        private const val TAG = "MapStreamManager"
        const val REQUEST_CODE_SCREEN_CAPTURE = 9001

        // Board là 240x240 (BOARD_LCD_H_RES/V_RES trong img_stream.c).
        private const val BOARD_WIDTH = 240
        private const val BOARD_HEIGHT = 240
        private const val JPEG_QUALITY = 35
        private const val DEFAULT_FPS = 4
        private const val INTER_FRAME_DELAY_MS = 30L
    }

    // Full screen capture dimensions (set khi start)
    private var captureFullWidth = 0
    private var captureFullHeight = 0

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var captureThread: HandlerThread? = null
    private var captureHandler: Handler? = null
    private var streamingThread: HandlerThread? = null
    private var streamingHandler: Handler? = null

    @Volatile
    private var isStreaming = false

    fun isStreamingActive(): Boolean = isStreaming

    @Volatile
    var fps: Int = DEFAULT_FPS
        set(value) {
            field = value.coerceIn(1, 10)
        }

    private var screenDensity: Int = 1

    /**
     * Call from Activity to get the MediaProjection permission intent.
     * Launch it with startActivityForResult(intent, REQUEST_CODE_SCREEN_CAPTURE).
     */
    fun getProjectionIntent(): Intent {
        val projectionManager = context.getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        return projectionManager.createScreenCaptureIntent()
    }

    /**
     * Call from onActivityResult after user grants screen capture permission.
     */
    fun onPermissionResult(resultCode: Int, data: Intent?) {
        if (resultCode != Activity.RESULT_OK || data == null) {
            Log.e(TAG, "Screen capture permission denied")
            return
        }
        val projectionManager = context.getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        mediaProjection = projectionManager.getMediaProjection(resultCode, data)
        Log.d(TAG, "MediaProjection obtained")
    }

    /**
     * Start capturing and streaming frames over BLE.
     * Must call onPermissionResult() first.
     */
    fun startStreaming() {
        if (isStreaming) {
            Log.w(TAG, "Already streaming")
            return
        }
        val projection = mediaProjection
        if (projection == null) {
            Log.e(TAG, "No MediaProjection available. Call onPermissionResult() first.")
            return
        }

        isStreaming = true

        // Get screen density + full resolution
        val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()
        @Suppress("DEPRECATION")
        wm.defaultDisplay.getRealMetrics(metrics)
        screenDensity = metrics.densityDpi
        captureFullWidth = metrics.widthPixels
        captureFullHeight = metrics.heightPixels
        Log.i(TAG, "Screen: ${captureFullWidth}x${captureFullHeight} density=$screenDensity")

        // Create capture thread
        captureThread = HandlerThread("MapCapture").also { it.start() }
        captureHandler = Handler(captureThread!!.looper)

        // Create streaming thread (for BLE writes with delays)
        streamingThread = HandlerThread("MapStream").also { it.start() }
        streamingHandler = Handler(streamingThread!!.looper)

        // ImageReader ở full screen resolution — cần full vì VirtualDisplay
        // AUTO_MIRROR phải khớp đúng kích thước màn hình thật, sau đó mới
        // crop lại đúng vùng bong bóng (tọa độ accessibility cũng là tọa độ
        // màn hình thật nên khớp trực tiếp, không cần quy đổi tỷ lệ).
        imageReader = ImageReader.newInstance(
            captureFullWidth, captureFullHeight,
            PixelFormat.RGBA_8888, 2
        )

        // Create VirtualDisplay (mirror full screen)
        virtualDisplay = projection.createVirtualDisplay(
            "MapStream",
            captureFullWidth, captureFullHeight, screenDensity,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            imageReader!!.surface,
            null, captureHandler
        )

        // Register MediaProjection stop callback
        projection.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                Log.d(TAG, "MediaProjection stopped by system")
                stopStreaming()
            }
        }, captureHandler)

        // Start frame capture loop
        captureHandler?.post(captureRunnable)
        Log.d(TAG, "Streaming started at $fps FPS")
    }

    /**
     * Stop streaming and release resources.
     */
    fun stopStreaming() {
        isStreaming = false
        virtualDisplay?.release()
        virtualDisplay = null
        imageReader?.close()
        imageReader = null
        captureThread?.quitSafely()
        captureThread = null
        captureHandler = null
        streamingThread?.quitSafely()
        streamingThread = null
        streamingHandler = null
        Log.d(TAG, "Streaming stopped")
    }

    /**
     * Release all resources including MediaProjection.
     */
    fun destroy() {
        stopStreaming()
        mediaProjection?.stop()
        mediaProjection = null
    }

    private val captureRunnable = object : Runnable {
        override fun run() {
            if (!isStreaming) return

            val frameIntervalMs = 1000L / fps
            val startTime = System.currentTimeMillis()

            try {
                captureAndSendFrame()
            } catch (e: Exception) {
                Log.e(TAG, "Frame capture error: ${e.message}")
            }

            // Schedule next frame
            val elapsed = System.currentTimeMillis() - startTime
            val nextDelay = (frameIntervalMs - elapsed).coerceAtLeast(10L)
            captureHandler?.postDelayed(this, nextDelay)
        }
    }

    private var frameCounter = 0
    private var nullImageCounter = 0
    private var noBubbleCounter = 0

    private fun captureAndSendFrame() {
        val reader = imageReader ?: return

        // Không có bong bóng đang hiển thị (app không chạy, hoặc bong bóng
        // đang tắt) → không có gì để chụp, bỏ qua frame này thay vì gửi rác.
        val bubbleBounds = VietmapAccessibilityService.bubbleBoundsInScreen
        if (bubbleBounds == null) {
            noBubbleCounter++
            if (noBubbleCounter % 40 == 1) {
                Log.w(TAG, "Chưa tìm thấy bong bóng VietMap Live (đã bật Accessibility Service + overlay permission chưa?)")
            }
            val stale = reader.acquireLatestImage()
            stale?.close()
            return
        }

        val image: Image? = reader.acquireLatestImage()
        if (image == null) {
            nullImageCounter++
            if (nullImageCounter % 20 == 1) {
                Log.w(TAG, "acquireLatestImage null (count=$nullImageCounter) - VirtualDisplay chưa có frame?")
            }
            return
        }

        try {
            val jpegBytes = imageToJpeg(image, bubbleBounds)
            if (jpegBytes != null && jpegBytes.size > 50) {
                frameCounter++
                if (frameCounter % 20 == 1) {
                    Log.i(TAG, "📷 Bubble frame #$frameCounter: ${jpegBytes.size} bytes → BLE")
                }
                if (frameCounter == 3) {
                    try {
                        val f = java.io.File(context.getExternalFilesDir(null), "bubble_debug.jpg")
                        java.io.FileOutputStream(f).use { it.write(jpegBytes) }
                        Log.i(TAG, "Debug: saved bubble frame to ${f.absolutePath}")
                    } catch (e: Exception) {
                        Log.e(TAG, "Debug save failed: ${e.message}")
                    }
                }
                // Send on streaming thread to avoid blocking capture
                streamingHandler?.post {
                    sendFrameOverBle(jpegBytes)
                }
            }
        } finally {
            image.close()
        }
    }

    private fun imageToJpeg(image: Image, bubbleBoundsInScreen: android.graphics.Rect): ByteArray? {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowPadding = rowStride - pixelStride * image.width

        // Full-screen bitmap từ Image (kèm padding stride của ImageReader).
        val fullWidth = image.width + rowPadding / pixelStride
        val fullBitmap = Bitmap.createBitmap(fullWidth, image.height, Bitmap.Config.ARGB_8888)
        fullBitmap.copyPixelsFromBuffer(buffer)

        // Crop đúng vùng bong bóng, kẹp trong biên ảnh thật (đề phòng bong
        // bóng vừa bị kéo ra sát mép/ngoài màn hình lúc chụp).
        val left = bubbleBoundsInScreen.left.coerceIn(0, image.width - 1)
        val top = bubbleBoundsInScreen.top.coerceIn(0, image.height - 1)
        val right = bubbleBoundsInScreen.right.coerceIn(left + 1, image.width)
        val bottom = bubbleBoundsInScreen.bottom.coerceIn(top + 1, image.height)

        val bubbleBitmap = try {
            Bitmap.createBitmap(fullBitmap, left, top, right - left, bottom - top)
        } catch (e: Exception) {
            Log.e(TAG, "Crop bubble thất bại: ${e.message}")
            fullBitmap.recycle()
            return null
        }
        fullBitmap.recycle()

        // Scale "contain" giữ nguyên tỷ lệ (không méo hình) vào khung vuông
        // của board, letterbox nền đen phần thừa — cùng cách đã dùng cho
        // luồng map trước đó (CarHostForegroundService.letterboxToSquare).
        val scale = minOf(
            BOARD_WIDTH.toFloat() / bubbleBitmap.width,
            BOARD_HEIGHT.toFloat() / bubbleBitmap.height,
        )
        val scaledW = (bubbleBitmap.width * scale).toInt().coerceAtLeast(1)
        val scaledH = (bubbleBitmap.height * scale).toInt().coerceAtLeast(1)

        val out = Bitmap.createBitmap(BOARD_WIDTH, BOARD_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = android.graphics.Canvas(out)
        canvas.drawColor(android.graphics.Color.BLACK)
        val scaledBitmap = Bitmap.createScaledBitmap(bubbleBitmap, scaledW, scaledH, true)
        bubbleBitmap.recycle()
        val dstLeft = (BOARD_WIDTH - scaledW) / 2f
        val dstTop = (BOARD_HEIGHT - scaledH) / 2f
        canvas.drawBitmap(scaledBitmap, dstLeft, dstTop, null)
        scaledBitmap.recycle()

        val outputStream = ByteArrayOutputStream(8192)
        out.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, outputStream)
        out.recycle()

        return outputStream.toByteArray()
    }

    private fun sendFrameOverBle(jpegBytes: ByteArray) {
        if (!isStreaming) return
        if (!imageRelay.isConnected) return

        imageRelay.sendJpegFrame(jpegBytes)

        // Inter-frame delay for ESP32 decode time
        try {
            Thread.sleep(INTER_FRAME_DELAY_MS)
        } catch (_: InterruptedException) {}
    }
}
