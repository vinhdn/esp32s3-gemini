package com.esp32nav.service

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.esp32nav.CarNavApplication
import kotlinx.coroutines.*
import java.io.BufferedReader
import java.io.InputStreamReader

/**
 * Service đọc logcat realtime để lấy dữ liệu TPMS và Speed Limit
 * từ InstrumentActivity (com.baic.ungdung/com.autoai.ungdung).
 *
 * Hoạt động trên Android 9 head unit BAIC với SELinux Permissive,
 * cho phép app đọc log từ process khác.
 *
 * Log patterns:
 * - TPMS: "InstrumentActivity: Fetched initial TPMS data: LF=208,7 RF=212,8 LR=218,3 RR=219,7"
 * - Speed limit: "InstrumentActivity: Speed limit sign updated: 50"
 * - Speed limit clear: "InstrumentActivity: Speed limit sign cleared to default"
 */
class LogcatReaderService : Service() {

    companion object {
        private const val TAG = "LogcatReader"

        // Volatile state accessible from outside
        @Volatile var lastTpmsFL: Int = -1  // kPa (e.g. 208 for 2.08 bar)
        @Volatile var lastTpmsFR: Int = -1
        @Volatile var lastTpmsRL: Int = -1
        @Volatile var lastTpmsRR: Int = -1
        @Volatile var lastSpeedLimit: Int = 0
        @Volatile var isRunning: Boolean = false
    }

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var logcatProcess: Process? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        isRunning = true
        Log.i(TAG, "✅ LogcatReader service started")
        startLogcatReader()
    }

    override fun onDestroy() {
        super.onDestroy()
        isRunning = false
        scope.cancel()
        logcatProcess?.destroy()
        Log.i(TAG, "LogcatReader service stopped")
    }

    private fun startLogcatReader() {
        scope.launch {
            while (isActive) {
                try {
                    // Read logcat filtering only InstrumentActivity tag
                    // -T 1: start from now (don't replay old logs)
                    // -s InstrumentActivity:D : only this tag at Debug level
                    val cmd = arrayOf("logcat", "-T", "1", "-s", "InstrumentActivity:D")
                    val process = Runtime.getRuntime().exec(cmd)
                    logcatProcess = process

                    val reader = BufferedReader(InputStreamReader(process.inputStream))
                    Log.i(TAG, "Logcat reader started, monitoring InstrumentActivity...")

                    var line: String?
                    while (isActive) {
                        line = reader.readLine()
                        if (line == null) {
                            // Process ended, break to restart
                            break
                        }
                        parseLine(line)
                    }

                    // Process ended or coroutine cancelled, cleanup
                    try { process.destroy() } catch (_: Exception) {}

                } catch (e: Exception) {
                    Log.e(TAG, "Logcat reader error: ${e.message}")
                }

                // Wait before retrying (avoids tight loop on persistent failures)
                if (isActive) {
                    delay(5000)
                }
            }
        }
    }

    private fun parseLine(line: String) {
        when {
            line.contains("Fetched initial TPMS data:") -> parseTpms(line)
            line.contains("Speed limit sign updated:") -> parseSpeedLimit(line)
            line.contains("Speed limit sign cleared") -> clearSpeedLimit()
        }
    }

    /**
     * Parse: "InstrumentActivity: Fetched initial TPMS data: LF=208,7 RF=212,8 LR=218,3 RR=219,7"
     * Values are kPa with comma as decimal separator (European format).
     * Convert to integer kPa (e.g. "208,7" -> 209 kPa).
     */
    private fun parseTpms(line: String) {
        try {
            val dataStr = line.substringAfter("TPMS data:").trim()
            // Parse each wheel: "LF=208,7"
            val lfMatch = Regex("""LF=(\d+)[,.]?(\d*)""").find(dataStr)
            val rfMatch = Regex("""RF=(\d+)[,.]?(\d*)""").find(dataStr)
            val lrMatch = Regex("""LR=(\d+)[,.]?(\d*)""").find(dataStr)
            val rrMatch = Regex("""RR=(\d+)[,.]?(\d*)""").find(dataStr)

            val fl = parseKpa(lfMatch)
            val fr = parseKpa(rfMatch)
            val rl = parseKpa(lrMatch)
            val rr = parseKpa(rrMatch)

            if (fl > 0 || fr > 0 || rl > 0 || rr > 0) {
                val changed = fl != lastTpmsFL || fr != lastTpmsFR ||
                        rl != lastTpmsRL || rr != lastTpmsRR

                lastTpmsFL = fl
                lastTpmsFR = fr
                lastTpmsRL = rl
                lastTpmsRR = rr

                if (changed) {
                    Log.i(TAG, "🛞 TPMS: FL=${fl} FR=${fr} RL=${rl} RR=${rr} kPa")
                    notifyTpmsUpdate(fl, fr, rl, rr)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse TPMS: ${e.message}")
        }
    }

    /**
     * Parse kPa value from regex match.
     * "208,7" -> integer part = 208, decimal part = 7 -> round to 209 kPa
     * "208" -> 208 kPa
     */
    private fun parseKpa(match: MatchResult?): Int {
        if (match == null) return -1
        val intPart = match.groupValues[1].toIntOrNull() ?: return -1
        val decPart = match.groupValues[2]
        // Round: if decimal >= 5, round up
        return if (decPart.isNotBlank()) {
            val firstDecDigit = decPart.first().digitToInt()
            if (firstDecDigit >= 5) intPart + 1 else intPart
        } else {
            intPart
        }
    }

    /**
     * Parse: "InstrumentActivity: Speed limit sign updated: 50"
     */
    private fun parseSpeedLimit(line: String) {
        try {
            val valueStr = line.substringAfter("updated:").trim()
            val limit = valueStr.toIntOrNull()
            if (limit != null && limit in 5..200 && limit != lastSpeedLimit) {
                lastSpeedLimit = limit
                Log.i(TAG, "🚫 Speed limit: $limit km/h")
                notifySpeedLimitUpdate(limit)
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse speed limit: ${e.message}")
        }
    }

    private fun clearSpeedLimit() {
        if (lastSpeedLimit != 0) {
            lastSpeedLimit = 0
            Log.i(TAG, "🚫 Speed limit cleared")
            notifySpeedLimitUpdate(0)
        }
    }

    private fun notifyTpmsUpdate(fl: Int, fr: Int, rl: Int, rr: Int) {
        val app = application as? CarNavApplication ?: return
        app.onLogcatTpmsUpdate(fl, fr, rl, rr)
    }

    private fun notifySpeedLimitUpdate(limit: Int) {
        val app = application as? CarNavApplication ?: return
        app.onLogcatSpeedLimitUpdate(limit)
    }
}
