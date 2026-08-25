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
import com.esp32nav.ble.BleManager
import java.io.ByteArrayOutputStream

/**
 * Captures the screen via MediaProjection, scales to 240x240,
 * compresses to JPEG, and streams over BLE to ESP32.
 *
 * BLE frame protocol (simple):
 * - Each JPEG frame is sent as sequential BLE writes (no response)
 * - ESP32 detects frame start by 0xFF 0xD8 (JPEG SOI marker)
 * - ESP32 detects frame end by 0xFF 0xD9 (JPEG EOI marker)
 * - Short delay between frames for ESP32 to decode
 */
class MapStreamManager(
    private val context: Context,
    private val bleManager: BleManager
) {
    companion object {
        private const val TAG = "MapStreamManager"
        const val REQUEST_CODE_SCREEN_CAPTURE = 9001

        // Output gửi tới ESP32 luôn là 240x240
        private const val CAPTURE_WIDTH = 240
        private const val CAPTURE_HEIGHT = 240
        private const val JPEG_QUALITY = 25
        private const val DEFAULT_FPS = 4
        private const val INTER_FRAME_DELAY_MS = 50L

        // Chế độ đơn giản hóa: lọc màu giữ đường, bỏ text/nền
        // true = chỉ hiện lines tuyến đường (nền đen, đường sáng)
        // false = ảnh map gốc
        const val SIMPLIFY_MODE_DEFAULT = true
    }

    // Full screen capture dimensions (set khi start)
    private var captureFullWidth = 0
    private var captureFullHeight = 0

    @Volatile
    var simplifyMode: Boolean = SIMPLIFY_MODE_DEFAULT

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
        Log.i(TAG, "Screen: ${captureFullWidth}x${captureFullHeight} density=$screenDensity, simplify=$simplifyMode")

        // Create capture thread
        captureThread = HandlerThread("MapCapture").also { it.start() }
        captureHandler = Handler(captureThread!!.looper)

        // Create streaming thread (for BLE writes with delays)
        streamingThread = HandlerThread("MapStream").also { it.start() }
        streamingHandler = Handler(streamingThread!!.looper)

        // ImageReader ở full screen resolution để crop được vùng map
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

    private fun captureAndSendFrame() {
        val reader = imageReader ?: return
        val image: Image? = reader.acquireLatestImage()
        if (image == null) {
            nullImageCounter++
            if (nullImageCounter % 20 == 1) {
                Log.w(TAG, "acquireLatestImage null (count=$nullImageCounter) - VirtualDisplay chưa có frame?")
            }
            return
        }

        try {
            val jpegBytes = imageToJpeg(image)
            if (jpegBytes != null && jpegBytes.size > 100) {
                frameCounter++
                if (frameCounter % 5 == 1) {
                    Log.i(TAG, "📷 Frame #$frameCounter: ${jpegBytes.size} bytes → BLE")
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

    private fun imageToJpeg(image: Image): ByteArray? {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowPadding = rowStride - pixelStride * image.width

        // Create full-screen bitmap from Image
        val fullWidth = image.width + rowPadding / pixelStride
        val fullBitmap = Bitmap.createBitmap(fullWidth, image.height, Bitmap.Config.ARGB_8888)
        fullBitmap.copyPixelsFromBuffer(buffer)

        // Crop vùng map: bỏ status bar trên (~10%) và bottom nav/instruction (~25%)
        // Google Maps: map ở giữa, instruction card ở dưới, search bar trên.
        // Lấy vùng trung tâm hình vuông (vùng map thuần).
        val srcW = image.width
        val srcH = image.height
        val cropTop = (srcH * 0.12f).toInt()      // bỏ 12% trên (search/status)
        val cropBottom = (srcH * 0.28f).toInt()   // bỏ 28% dưới (instruction card)
        val cropH = srcH - cropTop - cropBottom
        val cropSize = minOf(srcW, cropH)         // hình vuông
        val cropX = (srcW - cropSize) / 2
        val cropY = cropTop + (cropH - cropSize) / 2

        val mapRegion = try {
            Bitmap.createBitmap(
                fullBitmap,
                cropX.coerceAtLeast(0),
                cropY.coerceAtLeast(0),
                cropSize.coerceAtMost(fullWidth - cropX),
                cropSize.coerceAtMost(srcH - cropY)
            )
        } catch (e: Exception) {
            fullBitmap
        }

        // Scale xuống 240x240
        val scaled = Bitmap.createScaledBitmap(mapRegion, CAPTURE_WIDTH, CAPTURE_HEIGHT, true)

        // Apply simplify filter nếu bật
        val finalBitmap = if (simplifyMode) simplifyMapColors(scaled) else scaled

        // Compress to JPEG
        val outputStream = ByteArrayOutputStream(8192)
        finalBitmap.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, outputStream)

        // Clean up
        if (mapRegion !== fullBitmap) mapRegion.recycle()
        if (scaled !== finalBitmap) scaled.recycle()
        if (finalBitmap !== scaled) finalBitmap.recycle()
        fullBitmap.recycle()

        return outputStream.toByteArray()
    }

    /**
     * Đơn giản hóa màu bản đồ: giữ đường (xanh navigation, xám/trắng đường),
     * làm nền đen, bỏ text/label để rõ tuyến đường + nén JPEG tốt hơn.
     */
    private fun simplifyMapColors(src: Bitmap): Bitmap {
        val w = src.width
        val h = src.height
        val pixels = IntArray(w * h)
        src.getPixels(pixels, 0, w, 0, 0, w, h)

        for (i in pixels.indices) {
            val p = pixels[i]
            val r = (p shr 16) and 0xFF
            val g = (p shr 8) and 0xFF
            val b = p and 0xFF

            val isNavBlue = (b > 150 && b > r + 30 && b > g + 10)        // tuyến đường xanh
            val isWhiteRoad = (r > 200 && g > 200 && b > 200)            // đường trắng
            val isGrayRoad = (kotlin.math.abs(r - g) < 25 &&
                              kotlin.math.abs(g - b) < 25 &&
                              r in 150..230)                              // đường xám
            val isYellowRoad = (r > 200 && g > 180 && b < 140)          // đường vàng (cao tốc)

            pixels[i] = when {
                isNavBlue -> 0xFF33BBFF.toInt()      // xanh sáng cho tuyến navigation
                isYellowRoad -> 0xFFFFCC33.toInt()   // vàng cho cao tốc
                isWhiteRoad -> 0xFFFFFFFF.toInt()    // trắng cho đường chính
                isGrayRoad -> 0xFF888888.toInt()     // xám cho đường phụ
                else -> 0xFF101510.toInt()           // nền tối (hơi xanh đen)
            }
        }

        val result = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        result.setPixels(pixels, 0, w, 0, 0, w, h)
        return result
    }

    private fun sendFrameOverBle(jpegBytes: ByteArray) {
        if (!isStreaming) return

        // Use BleManager's sendJpegFrame which handles chunking
        bleManager.sendJpegFrame(jpegBytes)

        // Inter-frame delay for ESP32 decode time
        try {
            Thread.sleep(INTER_FRAME_DELAY_MS)
        } catch (_: InterruptedException) {}
    }
}
