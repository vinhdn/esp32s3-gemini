package com.esp32nav.service

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.esp32nav.CarNavApplication
import com.esp32nav.parser.DatMapParser
import com.esp32nav.parser.GoogleMapsParser

class NavigationListenerService : NotificationListenerService() {

    companion object {
        private const val TAG = "NavListener"
        private const val GOOGLE_MAPS_PACKAGE = "com.google.android.apps.maps"
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        when (sbn.packageName) {
            GOOGLE_MAPS_PACKAGE -> handleGoogleMaps(sbn)
            DatMapParser.DATMAP_PACKAGE -> handleDatMap(sbn)
        }
    }

    private fun handleGoogleMaps(sbn: StatusBarNotification) {
        val notification = sbn.notification ?: return
        if (notification.category != Notification.CATEGORY_NAVIGATION) return

        val extras = notification.extras ?: return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
        val subText = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString() ?: ""

        Log.d(TAG, "Google Maps nav - title: $title, text: $text, subText: $subText")

        // Notification format:
        // title: "1.7 km" (distance to next turn)
        // text: "CT37 Đ. Vành Đai 3" (road name)
        // subText: "36 min · 12 km · 10:20 AM ETA"
        val app = application as? CarNavApplication ?: return

        // Parse subText: "36 min · 12 km · 10:20 AM ETA"
        var timeRemaining = ""
        var totalDistance = ""
        var eta = ""
        if (subText.isNotBlank()) {
            val parts = subText.split("·", "•").map { it.trim() }
            for (part in parts) {
                when {
                    part.contains("min") || part.contains("hr") || part.contains("h") -> timeRemaining = part
                    part.contains("km") || part.contains("mi") || part.contains("m") -> {
                        if (totalDistance.isBlank()) totalDistance = part
                    }
                    part.contains("AM") || part.contains("PM") || part.contains(":") -> {
                        eta = part.replace("ETA", "").trim()
                    }
                }
            }
        }

        app.onGoogleMapsNavUpdate(
            direction = "",  // Notification không có direction info
            distance = title, // title chứa distance "1.7 km"
            roadName = text,  // text chứa road name
            instruction = "",
            timeRemaining = timeRemaining,
            totalDistance = totalDistance,
            eta = eta
        )
    }

    private fun handleDatMap(sbn: StatusBarNotification) {
        val notification = sbn.notification ?: return
        val extras = notification.extras ?: return

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()

        Log.d(TAG, "DatMap notification - title: $title, text: $text")

        val datMapData = DatMapParser.parseNotification(title, text) ?: return

        val app = application as? CarNavApplication ?: return
        app.onDatMapUpdate(datMapData)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.packageName != GOOGLE_MAPS_PACKAGE) return
        val notification = sbn.notification ?: return
        if (notification.category != Notification.CATEGORY_NAVIGATION) return

        Log.d(TAG, "Navigation notification removed")
        val app = application as? CarNavApplication ?: return
        app.onNavigationCleared()
    }
}
