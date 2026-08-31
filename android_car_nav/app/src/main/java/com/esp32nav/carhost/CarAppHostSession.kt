package com.esp32nav.carhost

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.res.Configuration
import android.graphics.Rect
import android.media.ImageReader
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.car.app.HandshakeInfo
import androidx.car.app.IAppHost
import androidx.car.app.IAppManager
import androidx.car.app.ICarApp
import androidx.car.app.ICarHost
import androidx.car.app.IOnDoneCallback
import androidx.car.app.ISurfaceCallback
import androidx.car.app.SurfaceContainer
import androidx.car.app.navigation.INavigationHost
import androidx.car.app.navigation.model.NavigationTemplate
import androidx.car.app.navigation.model.RoutingInfo
import androidx.car.app.serialization.Bundleable
import androidx.car.app.versioning.CarAppApiLevels

private const val TAG = "CarAppHostSession"

// Package/component của CarAppService thật trong VIETMAP Live 3.3.4. Bản
// VietMap phải được patch "android:debuggable=true" (xem
// patches/vietmap-live-3.3.4/) để HostValidator mặc định của Car App Library
// (androidx.car.app.CarAppService#createHostValidator) cho phép host tự viết
// này bind — bản release gốc chỉ allowlist host của Google.
private const val VIETMAP_PACKAGE = "vn.vietmap.live"
private const val VIETMAP_AA_SERVICE =
    "vn.vietmap.vietmap_live_navigation_plugin.android_auto2.VIETMAPLiveAndroidAutoService"
private const val ACTION_CAR_APP_SERVICE = "androidx.car.app.CarAppService"

/**
 * Tự host androidx.car.app.CarAppService của VietMap Live trong tiến trình
 * của chính app này — bind thẳng, không qua Android Auto/DHU thật. Mục đích:
 * lấy Surface mà VietMap vẽ map + HUD lên, đọc từng frame ra Bitmap.
 *
 * Lưu ý: onStableAreaChanged ở đây KHÔNG tương đương "ép padding" — patch
 * cluster-padding.patch (Ta/b.g/onVisibleAreaChanged) vẫn cần thiết vì
 * onVisibleAreaChanged được client tự tính nội bộ, không phải giá trị do
 * host truyền qua binder (ISurfaceCallback không có method này).
 */
class CarAppHostSession(private val context: Context) {

    fun interface FrameListener {
        fun onFrame(reader: ImageReader)
    }

    var frameListener: FrameListener? = null

    // Dữ liệu dẫn đường lấy qua NavigationTemplate (IAppManager.getTemplate),
    // KHÔNG phải qua VmslRelay/VMSX (đó là kênh JSON nội bộ riêng, không đi
    // qua androidx.car.app). Xem phân tích trong lịch sử hội thoại.
    data class NavInfo(
        val cue: String?,
        val road: String?,
        val maneuverType: Int?,
        val currentDistanceMeters: Double?,
        val hasJunctionImage: Boolean,
        val remainingDistanceMeters: Double?,
        val remainingTimeSeconds: Long?,
    )

    fun interface NavInfoListener {
        fun onNavInfo(info: NavInfo)
    }

    var navInfoListener: NavInfoListener? = null

    @Volatile
    private var carApp: ICarApp? = null

    @Volatile
    private var surfaceCallback: ISurfaceCallback? = null

    private var imageReader: ImageReader? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            Log.i(TAG, "onServiceConnected: $name")
            val app = ICarApp.Stub.asInterface(binder)
            carApp = app
            startHandshake(app)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            Log.w(TAG, "onServiceDisconnected: $name")
            carApp = null
            surfaceCallback = null
        }
    }

    fun bind() {
        val intent = Intent(ACTION_CAR_APP_SERVICE)
            .setComponent(ComponentName(VIETMAP_PACKAGE, VIETMAP_AA_SERVICE))
        val ok = context.bindService(intent, connection, Context.BIND_AUTO_CREATE)
        Log.i(TAG, "bindService -> $ok")
    }

    fun unbind() {
        try {
            context.unbindService(connection)
        } catch (e: IllegalArgumentException) {
            // chưa từng bind thành công, bỏ qua
        }
        imageReader?.close()
        imageReader = null
        carApp = null
        surfaceCallback = null
    }

    private fun doneCallback(tag: String): IOnDoneCallback = object : IOnDoneCallback.Stub() {
        override fun onSuccess(response: Bundleable?) {
            Log.i(TAG, "$tag onSuccess")
        }

        override fun onFailure(failure: Bundleable?) {
            Log.e(TAG, "$tag onFailure: ${failure?.let { runCatching { it.get() }.getOrNull() }}")
        }
    }

    private fun startHandshake(app: ICarApp) {
        val handshake = HandshakeInfo(context.packageName, CarAppApiLevels.getLatest())
        app.onHandshakeCompleted(Bundleable.create(handshake), object : IOnDoneCallback.Stub() {
            override fun onSuccess(response: Bundleable?) {
                Log.i(TAG, "handshake onSuccess")
                // Host thật (Google Android Auto) gọi getAppInfo() ngay sau
                // handshake, trước onAppCreate — ta chưa từng gọi bước này.
                // Thử xem có phải thiếu bước này khiến Dart chưa coi phiên
                // là "sẵn sàng" để bắt đầu đẩy setTrip/sendVMLCommonData.
                app.getAppInfo(object : IOnDoneCallback.Stub() {
                    override fun onSuccess(response: Bundleable?) {
                        val info = response?.let { runCatching { it.get() }.getOrNull() }
                        Log.i(TAG, "getAppInfo onSuccess: $info")
                        onAppCreate(app)
                    }

                    override fun onFailure(failure: Bundleable?) {
                        val value = failure?.let { runCatching { it.get() }.getOrNull() }
                        Log.e(TAG, "getAppInfo onFailure: $value")
                        onAppCreate(app)
                    }
                })
            }

            override fun onFailure(failure: Bundleable?) {
                Log.e(TAG, "handshake onFailure: ${failure?.let { runCatching { it.get() }.getOrNull() }}")
            }
        })
    }

    private fun onAppCreate(app: ICarApp) {
        val launchIntent = Intent(Intent.ACTION_MAIN)
            .setComponent(ComponentName(VIETMAP_PACKAGE, VIETMAP_AA_SERVICE))
        val configuration = Configuration(context.resources.configuration)
        app.onAppCreate(
            carHostStub,
            launchIntent,
            configuration,
            doneCallback("onAppCreate"),
        )
    }

    // ICarHost do CHÍNH TA (host) implement. VietMap gọi getHost(type) để lấy
    // binder cho từng "manager" phía host: "app" (thực đo được, khác tài liệu
    // ghi "AppHost") -> IAppHost (setSurfaceCallback/invalidate/...), và có
    // thể "Navigation" -> INavigationHost (updateTrip/navigationStarted...)
    // nếu VietMap dùng CarNavigationManager (đã xác nhận có tham chiếu class
    // này trong code). Log nguyên type để biết chính xác key thật khi test.
    private val carHostStub = object : ICarHost.Stub() {
        override fun getHost(type: String): IBinder {
            Log.i(TAG, "getHost($type)")
            return when {
                type.contains("Navigation", ignoreCase = true) -> navigationHostStub.asBinder()
                else -> appHostStub.asBinder()
            }
        }

        override fun startCarApp(intent: Intent) {
            Log.i(TAG, "startCarApp: $intent")
        }

        override fun finish() {
            Log.i(TAG, "finish() do VietMap yêu cầu")
        }
    }

    private val navigationHostStub = object : INavigationHost.Stub() {
        override fun navigationStarted() {
            Log.i(TAG, "INavigationHost.navigationStarted()")
        }

        override fun navigationEnded() {
            Log.i(TAG, "INavigationHost.navigationEnded()")
        }

        override fun updateTrip(trip: Bundleable?) {
            val value = trip?.let { runCatching { it.get() }.getOrNull() }
            Log.i(TAG, "INavigationHost.updateTrip: $value")
        }
    }

    private val appHostStub = object : IAppHost.Stub() {
        override fun setSurfaceCallback(callback: ISurfaceCallback?) {
            Log.i(TAG, "setSurfaceCallback: $callback")
            surfaceCallback = callback
            provideSurface()
        }

        override fun invalidate() {
            Log.i(TAG, "invalidate()")
            requestTemplate()
        }

        override fun showToast(text: CharSequence?, duration: Int) {
            Log.i(TAG, "showToast: $text")
        }

        override fun showAlert(alert: Bundleable?) {
            Log.i(TAG, "showAlert")
        }

        override fun dismissAlert(alertId: Int) {
            Log.i(TAG, "dismissAlert: $alertId")
        }

        override fun sendLocation(location: android.location.Location?) {
            // không cần cho việc capture bitmap
        }

        override fun openMicrophone(params: Bundleable?): Bundleable {
            throw UnsupportedOperationException("openMicrophone chưa hỗ trợ")
        }
    }

    private fun provideSurface() {
        val cb = surfaceCallback ?: return

        // Khung 16:9 - ty le man hinh Android Auto thuc te tren dau xe (khac
        // voi khung vuong ban dau gay zoom qua sau vi VietMap hieu la khung
        // qua nho, va khac voi khung doc 480x800 dung tam thoi truoc do de
        // vi tri xe bam camera cho dung). Letterbox lai thanh vuong o
        // CarHostForegroundService truoc khi gui board, khong ep meo ty le.
        val width = 480
        val height = 270
        val reader = ImageReader.newInstance(width, height, android.graphics.PixelFormat.RGBA_8888, 2)
        imageReader = reader
        // setSurfaceCallback() (nơi gọi provideSurface) chạy trên Binder
        // threadpool thread, không có Looper — handler=null sẽ throw
        // "handler is null but the current thread is not a looper".
        reader.setOnImageAvailableListener(
            { frameListener?.onFrame(reader) },
            Handler(Looper.getMainLooper()),
        )

        // DPI thật của điện thoại host (~560 tren Pixel 3XL, "xxxhdpi") khien
        // VietMap hieu surface 480x480px chi la khung nhin ~137dp - nho hon
        // ca smartwatch. Neu logic zoom/framing cua map tinh theo kich thuoc
        // dp, khung qua nho co the ep zoom rat sau. Dung dpi chuan (mdpi,
        // 160) de 480px duoc hieu la ~480dp - tuong duong chieu ngang 1
        // dien thoai binh thuong, gan voi kich thuoc VietMap von quen xu ly.
        val surfaceDpi = 160
        val container = SurfaceContainer(reader.surface, width, height, surfaceDpi)
        cb.onSurfaceAvailable(Bundleable.create(container), doneCallback("onSurfaceAvailable"))

        // Không có onVisibleAreaChanged ở tầng binder (client tự suy ra nội
        // bộ) — chỉ có onStableAreaChanged. Báo full-size, không ép padding
        // ở đây; padding thật vẫn do cluster-padding.patch đảm nhiệm.
        cb.onStableAreaChanged(Rect(0, 0, width, height), doneCallback("onStableAreaChanged"))
    }

    fun start() {
        val app = carApp ?: return
        app.onAppStart(doneCallback("onAppStart"))
        app.onAppResume(doneCallback("onAppResume"))
        // Lấy template ngay lần đầu, không đợi invalidate() (VietMap có thể
        // không gọi invalidate() cho đến khi có thay đổi thật).
        requestTemplate()
    }

    // getTemplate() nằm trên IAppManager, KHÔNG phải ICarApp — phải xin
    // IAppManager qua ICarApp.getManager("app", cb) trước (đúng bằng chứng
    // trong CarAppBinder.lambda$getManager$7: chỉ nhận "app"/"navigation",
    // sai type ném InvalidParameterException). Kết quả trả về qua
    // RemoteUtils.g(...) — có thể là chính IAppManager (Bundleable tự biết
    // marshal IInterface) hoặc raw IBinder tuỳ bản library, nên thử cả hai.
    private fun requestTemplate() {
        val app = carApp ?: return
        app.getManager("app", object : IOnDoneCallback.Stub() {
            override fun onSuccess(response: Bundleable?) {
                val obj = response?.let { runCatching { it.get() }.getOrNull() }
                Log.i(TAG, "getManager(app) onSuccess: ${obj?.javaClass?.name}")
                val appManager = when (obj) {
                    is IAppManager -> obj
                    is IBinder -> IAppManager.Stub.asInterface(obj)
                    else -> null
                }
                if (appManager == null) {
                    Log.e(TAG, "Không lấy được IAppManager từ response: $obj")
                    return
                }
                appManager.getTemplate(object : IOnDoneCallback.Stub() {
                    override fun onSuccess(response: Bundleable?) {
                        val wrapper = response?.let { runCatching { it.get() }.getOrNull() }
                        Log.i(TAG, "getTemplate onSuccess: ${wrapper?.javaClass?.name}")
                        // getTemplate() trả về TemplateWrapper (đo thực tế),
                        // NavigationTemplate thật nằm trong .getTemplate() của
                        // wrapper này, không phải chính response.
                        val template = (wrapper as? androidx.car.app.model.TemplateWrapper)?.template
                        if (template is NavigationTemplate) {
                            logNavigationTemplate(template)
                        } else if (template != null) {
                            Log.i(TAG, "Template khác NavigationTemplate: ${template.javaClass.name}")
                        } else {
                            Log.i(TAG, "TemplateWrapper.template null, wrapper=$wrapper")
                        }
                    }

                    override fun onFailure(failure: Bundleable?) {
                        val value = failure?.let { runCatching { it.get() }.getOrNull() }
                        Log.e(TAG, "getTemplate onFailure: $value")
                    }
                })
            }

            override fun onFailure(failure: Bundleable?) {
                val value = failure?.let { runCatching { it.get() }.getOrNull() }
                Log.e(TAG, "getManager(app) onFailure: $value")
            }
        })
    }

    private fun logNavigationTemplate(template: NavigationTemplate) {
        val navInfo = template.navigationInfo
        val estimate = template.destinationTravelEstimate
        Log.i(TAG, "NavigationTemplate.navigationInfo = $navInfo (${navInfo?.javaClass?.name})")
        Log.i(TAG, "NavigationTemplate.destinationTravelEstimate = $estimate")

        if (navInfo is RoutingInfo) {
            val current = navInfo.currentStep
            val next = navInfo.nextStep
            Log.i(
                TAG,
                "RoutingInfo currentStep cue='${current?.cue}' road='${current?.road}' " +
                    "maneuverType=${current?.maneuver?.type} lanes=${current?.lanes?.size ?: 0}",
            )
            Log.i(TAG, "RoutingInfo currentDistance = ${navInfo.currentDistance}")
            Log.i(TAG, "RoutingInfo junctionImage present = ${navInfo.junctionImage != null}")
            Log.i(TAG, "RoutingInfo nextStep cue='${next?.cue}' road='${next?.road}'")
            Log.i(TAG, "RoutingInfo isLoading = ${navInfo.isLoading}")

            navInfoListener?.onNavInfo(
                NavInfo(
                    cue = current?.cue?.toString(),
                    road = current?.road?.toString(),
                    maneuverType = current?.maneuver?.type,
                    currentDistanceMeters = navInfo.currentDistance?.displayDistance,
                    hasJunctionImage = navInfo.junctionImage != null,
                    remainingDistanceMeters = estimate?.remainingDistance?.displayDistance,
                    remainingTimeSeconds = estimate?.remainingTimeSeconds,
                ),
            )
        }
    }
}
