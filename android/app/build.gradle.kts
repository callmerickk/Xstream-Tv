plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.callmerickk.xstreamtv"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.callmerickk.xstreamtv"
        minSdk = 21
        targetSdk = 35
        versionCode = 1
        versionName = "0.1 preview"
        ndk { abiFilters += listOf("armeabi-v7a", "arm64-v8a") }
    }
    // The fonts are shared with the desktop app.
    sourceSets["main"].assets.srcDirs("../../assets")
    buildTypes { release { isMinifyEnabled = false; signingConfig = signingConfigs.getByName("debug") } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}

dependencies { implementation(project(":xehost")) }
