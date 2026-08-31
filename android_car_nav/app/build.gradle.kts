plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.esp32nav"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.esp32nav"
        minSdk = 28  // Car API requires API 28+
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
    }

    signingConfigs {
        create("release") {
            storeFile = file("car-nav.jks")
            storePassword = "carnav123"
            keyAlias = "carnav"
            keyPassword = "carnav123"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.material.icons.extended)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.gson)
    debugImplementation(libs.androidx.ui.tooling)

    // Car App Library — dùng để tự host androidx.car.app.CarAppService của
    // VietMap Live (bind trực tiếp, không qua Android Auto/DHU thật) và lấy
    // Surface render ra làm bitmap. Phải khớp version VietMap 3.3.4 bundle
    // (xem META-INF/androidx.car.app_app.version trong APK đã decompile).
    implementation("androidx.car.app:app:1.4.0")

    // Android Car API - provided by the system on Android Automotive OS
    // Compile-only: the actual implementation is on the device
    // UNCOMMENT sau khi pull android.car.jar từ thiết bị:
    //   ./pull_car_jar.sh
    // compileOnly(files("libs/android.car.jar"))
}
