plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.xe.xehost"
    compileSdk = 35

    defaultConfig {
        minSdk = 21
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += listOf("armeabi-v7a", "arm64-v8a") }
        externalNativeBuild {
            cmake { arguments += listOf("-DXE_GLES=ON", "-DXE_BUILD_WINDOWS_HOST=OFF", "-DXE_BUILD_P1_TEST=OFF", "-DXE_BUILD_ANDROID_HOST=ON") }
        }
    }

    externalNativeBuild { cmake { path = file("../CMakeLists.txt"); version = "3.22.1" } }
    buildFeatures { buildConfig = true }
}
